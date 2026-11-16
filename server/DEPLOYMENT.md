# 服务端发布与运维

## 运行条件

发布包由 `server/package.ps1` 从 Release 构建生成，面向 Windows x64。目标机器不需要 C++ 编译器，但需要：

- 可写的服务端数据目录。
- TCP 监听端口和相应的防火墙规则。
- 发布包自带的 Qt DLL、`sqldrivers/qsqlite.dll` 和 `libsodium.dll`。
- 如需真实 DeepSeek 推荐，只在该服务器进程环境中设置 `MEDIA_SERVER_DEEPSEEK_API_KEY`。

发布包不得包含 API Key、正式数据库、真实人物映射或真实 feed 配置。建议正式库和演示库使用两个完全独立的目录、端口和账号集合。

## 生成发布包

```powershell
cmake --build .\build --config Release
.\server\package.ps1 `
  -BuildDirectory .\build `
  -QtBin C:\msys64\ucrt64\bin `
  -SodiumDll C:\msys64\ucrt64\bin\libsodium.dll `
  -OutputDirectory .\release\media-server-2026-11-16
```

脚本要求输出目录尚不存在；不会覆盖已有发布目录、数据库或组员本地文件。脚本会运行每个服务端程序的 `--help` 做 DLL 和启动冒烟检查，并确认 SQLite 驱动存在。

## 首次初始化

正式库和演示库分开：

```powershell
New-Item -ItemType Directory -Force D:\media-server\formal
.\media_server.exe --db D:\media-server\formal\server.sqlite --init

Copy-Item D:\media-server\formal\server.sqlite D:\media-server\backups\server-initial.sqlite
```

初始化会打开现有 SQLite 连接并执行版本迁移。初次创建后应立即备份空库；之后每次升级、导入人物或修改 feed 前再次备份。

创建两个演示账号，密码由标准输入读取，不写在命令行或发布包中：

```powershell
.\media_server.exe --db D:\media-server\formal\server.sqlite --add-user alice
.\media_server.exe --db D:\media-server\formal\server.sqlite --add-user bob
```

账号只是网络身份。公私有资源隔离必须在 Qt 客户端中分别登录 alice、bob，并用公开仓库、alice 私有仓库和第三方私有仓库进行验收。

## 人物映射与 feed

先停止服务器、备份数据库，再执行人物映射导入：

```powershell
.\media_person_import.exe D:\media-server\formal\server.sqlite D:\ops\reviewed-person-mapping.json
```

人物目录导入后，再登记已经人工核验的 HTTPS feed：

```powershell
.\media_person_source_config.exe `
  --db D:\media-server\formal\server.sqlite `
  --config D:\ops\person-sources.json
```

feed 配置格式和函数接口见 `PERSON_SOURCE_CONFIG.md`。该工具只追加，不删除来源、不修改已有成功时间或刷新代次，也不会清空资讯缓存。服务器没有接收客户端任意抓取 URL 的 RPC；服务端运维工具使用已配置的来源刷新缓存：

~~~powershell
& .\media_person_source_config.exe --db D:\demo\server.sqlite --refresh-person person:example
~~~

将示例人物 ID 换成已导入并确认来源的稳定 ID。刷新成功后可在关注中心重新选择该人物查看资讯。刷新失败时旧资讯会标记为过期缓存。

配置错误必须明确失败。例如未知 `person_id` 返回 `NotFound` 且整批不写入；HTTP、带账号密码或带 fragment 的 URL 返回 `InvalidArgument`。不要通过改数据库绕过这些检查。

## DeepSeek

Key 只放在服务器进程环境中：

```powershell
$env:MEDIA_SERVER_DEEPSEEK_API_KEY = Read-Host "DeepSeek API Key"
```

不要把 Key 写入仓库、发布包、命令行参数、人物映射或 feed 配置。发布包不提供 Key 样例文件。没有 Key 时推荐模块应使用既有 fallback，不影响普通仓库、权限、人物和资讯配置。

## 启动与重启

```powershell
$env:MEDIA_SERVER_DEEPSEEK_API_KEY = Read-Host "DeepSeek API Key"
.\media_server.exe --db D:\media-server\formal\server.sqlite --listen 0.0.0.0 --port 8765
```

局域网验收时，在服务器防火墙开放所选 TCP 端口，并让两台 Qt 客户端连接同一地址和端口。重启服务器后重新登录，核对账号、关注、公开/私有仓库权限和历史/资讯仍按数据库状态恢复。

## 数据库升级

1. 停止服务器。
2. 停止期间备份数据库文件；如存在 `-wal`、`-shm` 文件也一并保存。
3. 运行新版本：

```powershell
.\media_server.exe --db D:\media-server\formal\server.sqlite --init
```

4. 检查退出码为 0，再启动服务器并执行健康检查。
5. 若迁移失败，保留原文件和错误输出；不要用空库覆盖正式库。

当前人物 schema 版本为 3。版本 1 会创建版本 2、3；版本 2 会无损增加 `person_sources.refresh_generation`。未知更高版本或不匹配结构会明确失败。

## 备份与恢复

推荐在停服状态下备份。最少备份：

- `server.sqlite`
- 同目录下可能存在的 `server.sqlite-wal`、`server.sqlite-shm`
- 本次使用的审核后人物映射和 feed 配置

恢复时先确认服务器已停止，再将这些文件恢复到同一版本目录。不要把演示库备份恢复到正式库，也不要把正式库复制到演示机。

## 验收清单

- 干净目录中四个服务端 EXE 均可从发布包启动 `--help`。
- 空目录执行 `--init` 成功，数据库重启后保持。
- alice、bob 两个账号可登录；公开仓库双方可读，私有仓库只允许本人。
- 错误人物 ID、HTTP feed、未知字段导致整批拒绝，数据库无半份写入。
- 人物映射只在确认后导入，同名人物不自动合并。
- 服务重启后账号、关注、仓库、人物目录、feed 配置和资讯缓存状态保持。
- 一台服务器同时连接两台 Qt 客户端；一方私有数据不出现在另一方结果中。
- 发布包和运行环境中没有明文 DeepSeek Key、正式库或真实映射泄漏。
