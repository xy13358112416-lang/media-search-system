# 多媒体文件管理及检索系统

这是一个 Windows Qt 桌面项目，用于管理、查找和推荐电影、电视剧和音乐作品。可以单机使用，也可以让两台电脑连接同一台仓库服务器，共享作品目录并分别查看各自的浏览足迹和推荐。

- **单机功能**包括媒体库、作品详情、关键词与导演／演员组合检索、今日推荐、媒体助手。本机管理员可新增和删除媒体记录。
- **联机功能**包括公私仓库、仓内搜索、关注中心、个人足迹、全站热榜、动态推荐和参与设置。服务器按网络账号检查权限。
- 随包提供 500 条作品元数据。样例没有附带音视频文件，格式和大小缺失时按未知处理。资料出处见 [样例数据说明](sample-data/DATA_SAMPLE_README.md)。

## 项目组成

~~~text
电脑 A 的 Qt 客户端 ─┐
                     ├─ 局域网 TCP ─ 电脑 A 的 media_server ─ 服务器 SQLite
电脑 B 的 Qt 客户端 ─┘                         │
                                                └─ DeepSeek（可选）
~~~

- [frontend/](frontend/) 是 Qt 桌面界面，入口为 `media_library_qt.exe`。
- [business/](business/) 管理本机 CSV、账户、角色和本机浏览历史。[retrieval/](retrieval/) 实现检索、推荐、热榜与足迹算法。
- [network/](network/) 封装 TCP 请求、登录会话和客户端接口。[server/](server/) 保存网络用户、仓库、行为、关注与资讯缓存，入口为 `media_server.exe`。
- [include/](include/) 定义共享接口。[sample-data/](sample-data/) 提供作品样例和自动测试用的模拟数据。

**本机账号和服务器账号分别登录。** Qt 首次启动时创建的是该电脑的本机管理员，账户保存在本机 `accounts.sqlite`；「资源仓库」使用服务器管理员创建的网络账号。联网数据都保存到服务端启动时指定的 SQLite 文件。同一份本机 CSV 无需在两台电脑之间同步。

## 两台电脑怎么联调

以下用电脑 A 同时运行服务器和第一个客户端，电脑 B 运行第二个客户端。两台电脑连同一可信局域网，使用同一构建版本。已拿到发布目录时，电脑 A 需要整个 `server` 和 `frontend` 目录，电脑 B 只需要整个 `frontend` 目录。目录内的 EXE、DLL 和插件要一起复制，目标电脑可以直接运行发布包。

### 1. 在电脑 A 建立测试库并启动服务

打开 PowerShell，进入发布包的 `server` 目录。第一次联调用独立的测试数据库，分别建立两个网络账号。执行每条创建账号的命令后，按提示输入该账号的测试密码。

~~~powershell
New-Item -ItemType Directory -Force .\data\demo
$DbPath = '.\data\demo\lan-test.sqlite'
.\media_server.exe --db $DbPath --init
.\media_server.exe --db $DbPath --add-user alice
.\media_server.exe --db $DbPath --add-user bob
~~~

用 `ipconfig` 查看电脑 A 的 IPv4 地址，选正在使用的 Wi-Fi 或以太网适配器。接着启动服务，输入查到的地址，例如 `192.168.1.23`。

~~~powershell
$ServerIp = Read-Host '电脑 A 的局域网 IPv4'
$Port = 8765
.\media_server.exe --db $DbPath --listen $ServerIp --port $Port
~~~

终端出现「仓库服务器已启动」并显示 IP 和端口后，保持该窗口打开。重新启动服务器时复用同一个 `--db` 路径即可。

### 2. 从电脑 B 测试端口并登录

在电脑 B 的 PowerShell 执行，输入**电脑 A**的地址。看到 `TcpTestSucceeded : True` 即可打开客户端。

~~~powershell
$ServerIp = Read-Host '电脑 A 的局域网 IPv4'
Test-NetConnection $ServerIp -Port 8765
~~~

在两台电脑上分别双击各自 `frontend` 目录里的 `media_library_qt.exe`。首次打开时按界面提示创建并登录本机账号。进入主窗口后，在「资源仓库」填写下面的信息并点击「连接并登录」。

- 服务器地址填电脑 A 的局域网 IPv4，端口填 `8765`。电脑 A 上的 Qt 同样填这个地址。
- 电脑 A 用 `alice`，电脑 B 用 `bob`。密码用刚才建网络账号时输入的值。
- 如果服务器改了端口，两个客户端和连通性测试都用实际端口。

### 3. 两个人一起验收

1. **公开和私有仓库**。Alice 在「新建仓库」中分别创建公开仓库和私有仓库。公开仓库勾选「允许其他账号查看」，两边各用「添加本地作品」添加一条记录。Bob 勾选「查看公开仓库」，只应看到 Alice 的公开仓库，能查看其中条目，无法编辑或删除。Alice 仍可操作自己仓库。
2. **搜索、足迹和账号隔离**。Bob 在公开仓库主动提交一次作品搜索，再点一次条目。在「个人足迹」中选覆盖今天的区间，核对搜索和浏览各增加一次。刷新、切页和自动选中不计浏览。Alice 的足迹不应随 Bob 操作变化。若没有新增行为，先检查「参与设置」里的历史开关。
3. **关注中心**。在已导入可信人物及作品关联的测试库中，搜索已知姓名或别名，查看关联作品、关注，再到「我的关注」确认，最后取关。分别用两个账号验证结果。人物关联需要导入，空库和普通模拟库不提供可信人物目录。见 [人物映射导入](server/PERSON_IMPORT.md)。
4. **推荐和热榜**。在「资源仓库」输入推荐需求，检查结果、理由及回退提示；在「动态推荐」比较两个有不同历史的账号。「全站热榜」要求同一项至少有三位用户公开贡献，两账号联调时空榜是预期结果。
5. **重启和换号**。退出网络账号后换一个账号，确认仓库、关注和足迹随账号变化。服务器按 `Ctrl+C` 停止，再用原数据库启动。重新登录后检查仓库、关注和历史仍在。

### 连不上时怎么查

- **端口测试失败**时，先看电脑 A 的服务器窗口是否还在运行，核对它监听的 IP 和端口。默认 `127.0.0.1` 只能供服务器电脑自身访问。还要确认两台电脑处在同一局域网，校园网或访客 Wi-Fi 可能隔离设备。
- **只有电脑 B 连不上**时，检查电脑 A 的 Windows 防火墙。如果确认是防火墙阻挡，在可信的专用网络上可添加一条仅允许局域网访问的入站规则。在电脑 A 的管理员 PowerShell 中进入 `server` 目录，把 IP 换成实际值再执行。

  ~~~powershell
  $ServerIp = '192.168.1.23'
  New-NetFirewallRule -DisplayName 'Media Search LAN 8765' -Direction Inbound -Action Allow -Protocol TCP -LocalAddress $ServerIp -LocalPort 8765 -RemoteAddress LocalSubnet -Profile Private -Program (Resolve-Path .\media_server.exe).Path
  ~~~

  联调结束后可用 `Remove-NetFirewallRule -DisplayName 'Media Search LAN 8765'` 移除这条规则。当前网络协议使用普通 TCP，联调在可信局域网进行。
- **端口通但密码不对**时，确认输入的是服务器网络账号，检查服务器本次启动的 `--db` 是否就是创建账号时用的文件。
- **登录成功但数据为空**时，先确认正在使用哪份数据库。新建服务器库默认没有仓库和历史。先按验收步骤创建一个公开仓库并添加条目。人物页面还需要确认过的关联数据。
- **启动报 Qt 插件或 DLL 错误**时，检查是否只复制了 EXE；从完整的发布目录启动。

## 从源码构建并启动

需要 C++17、CMake、Ninja、Qt 6（Core、Network、Sql、Widgets、Test）和通过 pkg-config 查找的 libsodium。生成模拟数据需 Python 3。配置 CMake 时，Qt 套件、编译器和 libsodium 要对应同一工具链。使用 MSYS2 UCRT64 时可在当前 PowerShell 执行 `. .\business\dev-env.ps1`；使用其他 Qt 套件时按本机安装设置路径。

在仓库根目录执行下面的命令。

~~~powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
.\build\frontend\media_library_qt.exe
~~~

命令行启动的客户端默认使用 EXE 同目录的 `media_records_sample.csv`，首次启动会生成可编辑的 `media_records.csv`。可用 `--csv "C:\path\media_records.csv"` 指定自备文件。使用源码构建联机时，按前面的流程操作，将服务端 EXE 换成仓库根目录下的 `.\build\server\media_server.exe`。

自动端到端检查可验证服务器在本机是否正常运行。

~~~powershell
python sample-data/simulation/demo_end_to_end_check.py . build
~~~

这个脚本使用临时库和本机端口验证两个模拟账号的足迹。若想在 Qt 里手动查看历史，可按 [模拟数据说明](sample-data/simulation/README.md#快速创建演示数据库) 生成并导入独立演示库，再用 `sim_user_01` 和 `sim_user_20` 登录。默认数据覆盖北京时间 2026-08-17 至 2026-11-15 零点，足迹页面请选择「自选区间」覆盖这段时间。

## 可选的 DeepSeek 配置

基础联调可以直接开始。如果要验收真实模型，在**启动服务器的 PowerShell** 中先设置 `MEDIA_SERVER_DEEPSEEK_API_KEY`。仓库推荐、热榜 AI 摘要和 DeepSeek 人物资讯统一使用这个变量名。更换 Key 后重启服务端。

~~~powershell
$SecureKey = Read-Host '输入 DeepSeek API Key' -AsSecureString
$env:MEDIA_SERVER_DEEPSEEK_API_KEY = [System.Net.NetworkCredential]::new('', $SecureKey).Password
Remove-Variable SecureKey
~~~

资源仓库在模型不可用时会给出规则回退提示。热榜中的「生成 AI 摘要」用于显式触发模型调用。人物资讯则由服务器运维工具按已核验的来源刷新，客户端读取缓存。真实资讯源、Key、成功与超时回退的验收步骤见 [资讯配置](server/PERSON_SOURCE_CONFIG.md) 和 [服务端部署](server/DEPLOYMENT.md)。

## 生成发布目录

Release 构建成功后，在仓库根目录用相同的 Qt 工具链分别打包。将下例的路径换成自己机器的实际路径，为两端分别指定还未创建的输出目录。

~~~powershell
$QtBin = 'C:\msys64\ucrt64\bin'
$SodiumDll = Join-Path $QtBin 'libsodium.dll'
.\frontend\package.ps1 -BuildDirectory .\build -QtBin $QtBin -SodiumDll $SodiumDll -OutputDirectory .\release\frontend
.\server\package.ps1 -BuildDirectory .\build -QtBin $QtBin -SodiumDll $SodiumDll -OutputDirectory .\release\server
~~~

最终交付记录包括同一个 Git 提交、两个发布目录、EXE 的 SHA-256、使用的数据来源和双机实测结果。参见 [验收记录](docs/final-acceptance.md)。
