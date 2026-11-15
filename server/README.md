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
