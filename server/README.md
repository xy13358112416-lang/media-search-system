# 媒体仓库服务器

服务器使用单独的 SQLite 文件保存服务器账户、仓库和媒体元数据快照，不修改本机的 `accounts.sqlite` 或媒体 CSV。服务器账户与单机账户是两套账户，需要分别创建和登录。每个人可以创建自己的公开或私有仓库；只有所有者能修改仓库条目，其他登录用户只能查看公开仓库。

先按照项目根目录说明编译。以下命令以 PowerShell、Windows 和示例数据库路径为例，执行前请在项目根目录加载 `business/dev-env.ps1`：

```powershell
. .\business\dev-env.ps1
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# 创建首个服务器账户；运行后按提示输入一行密码。
.\build\server\media_server.exe --db .\warehouse_server.sqlite --init-admin admin
# 创建另一名服务器用户。
.\build\server\media_server.exe --db .\warehouse_server.sqlite --add-user reader
# 默认监听本机，先在同一台电脑上联调。
.\build\server\media_server.exe --db .\warehouse_server.sqlite --listen 127.0.0.1 --port 12345
```

终端输入密码时可能会显示字符，不要使用真实账户的密码。`--init-admin` 只能在没有任何服务器账户时执行；普通 `--add-user` 用于添加其他用户。服务器账户没有单机系统中的管理员角色，仓库写权限按所有者判断。

两台电脑联调时，可将监听地址改为 `--listen 0.0.0.0`，客户端连接服务器所在电脑的局域网 IP 和同一端口；还要确保两台设备在同一局域网且服务器端口可通。服务器必须持续运行。停止服务器后数据库文件保留。

目前协议是明文 TCP，只用于可信局域网演示；不要开放到公网。服务器不传输音视频文件，也没有网络注册页面；用户由运行服务器的一方预先创建。

`WarehouseServer::readableWarehouses` 是供后续推荐接线使用的服务器内部接口：传入已认证连接对应的用户 ID，返回该用户拥有或公开可读的仓库及完整媒体快照；失败时不修改输出。为避免一次加载过多数据，接口限制最多 2000 个仓库、1000 个条目和 4 MiB 媒体 JSON；超限返回错误，不截断结果。规模扩大后可与推荐模块一起设计分页或流式预筛。它不接收网络客户端提交的用户身份，也不调用模型。后续异步推荐返回前仍须重新检查连接、会话和访问权限。
