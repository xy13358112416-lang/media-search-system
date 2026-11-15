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
