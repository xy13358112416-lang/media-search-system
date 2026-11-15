# 关怀联调版

## Windows 运行

客户端和服务器各自放在含运行依赖的目录。客户端直接打开 `media_library_qt.exe`。正常登录使用本机账号，进入「资源仓库」后另用服务器创建的网络账号。

在服务器目录的 PowerShell 中运行：

```powershell
.\media_server.exe --db warehouse.sqlite --init
.\media_server.exe --db warehouse.sqlite --add-user alice
.\media_server.exe --db warehouse.sqlite --add-user bob
.\media_server.exe --db warehouse.sqlite --listen 127.0.0.1 --port 8765
```

按提示给两个账号分别输入演示密码。双客户端跨电脑联调时，服务器用局域网地址替换 `127.0.0.1`，客户端输入该地址和端口。服务端可以配置 `MEDIA_SERVER_DEEPSEEK_API_KEY`。无密钥时，有候选的请求会收到明确的备用推荐。仓库登录使用现有局域网协议，只用于可信演示网络。

## 演示流程

1. 用两个网络账号分别连接服务器。一个账号创建公开及私有仓库，添加作品。另一个账号查看公开仓库，确认不能修改，也看不到私有仓库。
2. 在仓库提交推荐需求，尝试取消、切页和确认入库。无密钥的回退说明、正常空结果分别展示。
3. 打开「媒体助手」，试用温暖模式、场景检索和关闭动效。
4. 打开「关怀演示」，试用关注中心、热榜、足迹和动态推荐。此窗口操作只作用于当次模拟会话。真实资讯、公共统计、线上足迹和个性化结果需要服务器实现共享的 CareService 契约。

运行全套自动化验证：

```powershell
ctest --test-dir <构建目录> --output-on-failure
```
