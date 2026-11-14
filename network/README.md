# 仓库网络契约（PR 0）

本次仅提供共享类型、媒体 JSON 编解码和 TCP 帧编解码；服务器和网络客户端分别由后续 PR 实现。服务器 SQLite 与本地账户数据库分离，媒体本体仍为本地 CSV；传输的是媒体元数据快照，不传音视频文件。协议细节供组员评审，确定后服务端和客户端须同时遵守。

## 帧和消息

每帧由 4 字节无符号大端长度和对应字节数的 UTF-8 JSON 对象组成；长度仅计算 JSON，范围 1 至 4 MiB。TCP 可能拆包或合包，使用 `FrameDecoder` 持续接收；非法长度、JSON 或非对象是协议错误，应断开连接。一个 `feed` 中若先有完整帧再遇到非法帧，已输出的完整对象仍有效。

每个请求是 `{"type":"操作名","req_id":"1","token":"...","payload":{...}}`。`req_id` 是客户端生成的非空字符串，在同一连接上对未完成请求唯一；除 `login` 外都带服务器登录返回的 token，客户端不得提交 userId 决定当前身份。登录请求不带 token。成功响应为 `{"type":"操作名_resp","req_id":"1","payload":{...}}`；失败响应为 `{"type":"error_resp","req_id":"1","code":"PermissionDenied","message":"可展示的错误"}`。响应必须原样带回 req_id。无法解析出 req_id 的错误帧应断开连接，不发送关联响应。

所有数据库 ID 在 JSON 中表示为正十进制字符串（C++ 为 `qint64`）；名字和 token 是 JSON 字符串，列表是 JSON 数组。服务端拒绝无效、溢出或非正 ID。`visibility` 仅为 `private` 或 `public`。以下是 `payload` 内的字段；不写字段表示空对象：

| 请求 type | 请求 payload | 成功响应 payload |
|---|---|---|
| `login` | `username`, `password` | `token`, `user` |
| `logout` | 空 | 空 |
| `list_my_warehouses` | 空 | `warehouses` 摘要数组 |
| `list_public_warehouses` | 空 | `warehouses` 公开仓库摘要数组 |
| `get_warehouse` | `warehouse_id` | `warehouse`, `items` |
| `create_warehouse` | `name`, `visibility` | `warehouse` |
| `add_item` | `warehouse_id`, `media` | `item` |
| `remove_item` | `warehouse_id`, `item_id` | 空 |

`user`：`{"user_id":"1","username":"alice"}`；`warehouse` 摘要：`{"warehouse_id":"2","name":"收藏","owner_id":"1","visibility":"public"}`；`item`：`{"item_id":"3","media":{...}}`。`media` 的十个字段由 `media_json` 定义：`id`、`file_name`、`file_type`、`file_size`、`director`、`actors`、`view_count`、`release_date`、`awards`、`category`；其中大小和浏览次数为无符号十进制字符串，演员与奖项为字符串数组，日期为 `YYYY-MM-DD`。ID、文件名和类型不能为空。字段必须存在，允许空字符串的字段及空数组仍须提供。

错误码使用 `warehouse_contract::ErrorCode` 名称：`InvalidArgument`、`NotConnected`、`NotAuthenticated`、`AuthenticationFailed`、`PermissionDenied`、`NotFound`、`Timeout`、`ProtocolError`、`ServerError`。成功不发送 `error_resp`。连接、超时、协议错误也可由客户端本地生成。`WarehouseService` 回调在所属线程调用，每个请求恰好完成一次；断连时未完成请求以错误结束。具体超时长度由客户端实现配置。

可访问的详情包括本人仓库和他人公开仓库；仅所有者可新增或删除条目。服务器从 token 判断用户，不能使用客户端声明的身份。登录 token 仅在服务端维护，退出或断开连接后失效。此阶段使用普通 TCP，登录密码和 token 未加密，仅用于可信局域网演示；不应放到公网或使用真实账户密码。

`warehouse_client` 当前是 CMake 接口占位目标，没有网络实现；`server/CMakeLists.txt` 是空骨架。PR 2、PR 4 分别实现服务端与客户端，Qt 页面与推荐算法不在本次范围。
