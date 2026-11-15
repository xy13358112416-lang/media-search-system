# 仓库网络契约（PR 0）

网络模块提供共享类型、媒体 JSON 编解码、TCP 帧编解码和异步仓库客户端；`server/` 提供独立服务器。服务器 SQLite 与本地账户数据库分离，传输的是媒体元数据快照，不传音视频文件。

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
| `recommend_resources` | `query`, `limit`, 可选 `warehouse_id` | `items`, `message`, `fallback` |
| `cancel_recommendation` | `target_req_id` | 空 |

`user`：`{"user_id":"1","username":"alice"}`；`warehouse` 摘要：`{"warehouse_id":"2","name":"收藏","owner_id":"1","visibility":"public"}`；`item`：`{"item_id":"3","media":{...}}`。`media` 的十个字段由 `media_json` 定义：`id`、`file_name`、`file_type`、`file_size`、`director`、`actors`、`view_count`、`release_date`、`awards`、`category`；其中大小和浏览次数为无符号十进制字符串，演员与奖项为字符串数组。ID 和文件名不能为空。元数据记录允许 `file_type` 为空，其日期未知时 `release_date` 也为空；其他日期使用 `YYYY-MM-DD`。所有字段仍须提供。

错误码使用 `warehouse_contract::ErrorCode` 名称：`InvalidArgument`、`NotConnected`、`NotAuthenticated`、`AuthenticationFailed`、`PermissionDenied`、`NotFound`、`Timeout`、`ProtocolError`、`ServerError`。成功不发送 `error_resp`。连接、超时、协议错误也可由客户端本地生成。`WarehouseService` 回调在所属线程调用，每个请求恰好完成一次；断连时未完成请求以错误结束。具体超时长度由客户端实现配置。

可访问的详情包括本人仓库和他人公开仓库；仅所有者可新增或删除条目。服务器从 token 判断用户，不能使用客户端声明的身份。登录 token 仅在服务端维护，退出或断开连接后失效。此阶段使用普通 TCP，登录密码和 token 未加密，仅用于可信局域网演示；不应放到公网或使用真实账户密码。

## 客户端接入

`warehouse_client` 为可链接的 CMake 静态库，头文件 `include/warehouse_client.h` 实现 `WarehouseService`。先异步连接并登录，再调用仓库接口。正常返回通过 `req_id` 关联；参数、断线等立即失败可以同步回调，调用方需要允许回调发生在句柄赋值前。每个请求只完成一次；迟到响应忽略。普通请求默认 8 秒、推荐默认 30 秒。

推荐成功响应的 `items` 可以为空，`message` 是整体说明，`fallback` 是是否降级；每项含 `warehouse_id`、`item_id`、`media` 和 `reason`。客户端不访问 DeepSeek，不读取 API Key。PR R03 完成前，当前服务器尚不识别推荐或取消操作，会返回 `InvalidArgument`；模拟服务器测试不代表真实模型已经接通。

推荐句柄仅在创建它的客户端对象和连接会话中有效。`cancelRecommendation` 先于结果处理时返回 true 并以 `Cancelled` 完成原回调；结果先完成或句柄无效则返回 false。取消消息独立发送并尽力通知服务器，断网不能保证送达。退出或换账号时取消旧会话等待中的请求并丢弃迟到响应；已发送但尚无可确认结果的增删或创建操作返回 `OutcomeUnknown`，应查询确认后再重试。

测试使用模拟服务器覆盖乱序、超时、取消和断线，并使用已合并的服务器验证双用户权限与账号切换；真实双机联调和界面接入属于后续工作。
