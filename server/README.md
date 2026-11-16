# 仓库服务器

从项目根目录使用已有 Qt 和 libsodium 配置构建。在构建目录运行：

```powershell
cmake --build . --target media_server warehouse_server_tests
ctest -R "^warehouse_server_tests$" --output-on-failure
./server/media_server.exe --db warehouse.sqlite --init
./server/media_server.exe --db warehouse.sqlite --add-user alice
./server/media_server.exe --db warehouse.sqlite --add-user bob
./server/media_server.exe --db warehouse.sqlite --listen 127.0.0.1 --port 8765
```

创建账号时从标准输入读取演示密码。局域网联调时用服务器的局域网地址替换 `--listen`。网络账号及仓库条目保存在指定 SQLite 中，使用本机独立的账号数据库不会授予网络权限。当前协议使用普通 TCP，适用于可信的演示局域网。

[帧、操作名和字段](../network/README.md) 与共享协议一致。仓库条目包含完整媒体元数据快照，服务器根据连接内 token 检查权限。推荐接线可在会话验证后通过 `readableCandidates` 取得本人仓库及公开仓库的条目。

## 服务端智能推荐

服务器在进程启动时读取 `MEDIA_SERVER_DEEPSEEK_API_KEY`；也可通过 `MEDIA_SERVER_DEEPSEEK_CONFIG_PATH` 指向服务端配置文件，客户端无需配置 Key。未配置 Key 或模型服务失败时，推荐模块返回带 `fallback` 标记和说明的规则结果；模型正常返回空结果时保留空列表。

`recommend_resources` 接受 `query`、`limit`（1–50）和可选的 `warehouse_id`。服务器验证当前连接的会话及目标仓库的可读权限，仅从本人仓库和公开仓库中构造候选，返回前再次确认会话与资源可读性。推荐异步完成，普通请求无需等待模型。`cancel_recommendation` 的 `target_req_id` 必须指向同一连接的推荐；取消先到时原请求收到 `Cancelled`，取消请求收到独立确认。退出或断开连接会取消该会话的待处理任务，迟到结果被丢弃。

当前单次可读快照最多 1000 个仓库、1000 条记录和 4 MiB 媒体 JSON，超过上限时返回错误，不截断候选后假装结果完整。读取快照仍在服务器线程同步完成；规模进一步增长时需要分页或后台查询。

当 R01 的 `warehouse_client.cpp` 已进入项目时，构建自动启用 `recommendation_client_tests`：使用真实网络客户端和本机服务器验证推荐回退及取消。模型请求使用模拟服务或无 Key 配置，自动测试不消耗 API 配额。
## 行为记录和历史

基于 R00 的 `get_resource`、`record_behavior`、`get_participation_settings`、`set_participation_settings` 和 `clear_history` 操作使用当前连接中的登录身份。服务器为可读的仓库条目生成并保存 `srv:` 作品标识；浏览事件要求作品标识与来源位置匹配。当前每个来源独立分配标识，不自动把不同来源的同名作品合并。

事件 ID 按用户去重，搜索或浏览时间由服务器记录；浏览快照在资源删除后仍供该用户的历史统计使用。公开贡献默认为关闭，关闭后撤销之前事件的贡献资格；暂停历史记录不会删除旧记录。`clear_history` 清除当前事件数据；当前版本尚未创建足迹或热榜派生缓存，后续接入缓存时需同步扩展清理事务。

`WarehouseServer::queryBehavior` 是仅供可信服务端调用的内部接口，调用方必须从已认证会话取得用户 ID，返回半开 UTC 秒区间内的记录和媒体快照。查询最多 10000 条且快照合计最多 16 MiB，超限或数据库失败返回空的 `optional`，调用方应报告错误，不能视作空历史。`publicEligible` 仅表示事件产生时的贡献资格，公开统计时仍需重新检查来源当前是否公开。
