# 在线仓库、行为与足迹联调

本文说明服务器和 Qt 的接口边界，以及可重复的本机演示步骤。需要先合入行为存储、`OnlineCareClient`、足迹接口和演示导入器（对应 PR #53、#54、#55、#56）；部分接口或命令在这些分支全部合入前不可用。这里的示例行为和用户都是模拟数据。

## 一、构建与准备演示数据

在 Windows PowerShell 的仓库根目录，使用已配置好的 Qt 6、C++、CMake、Ninja、Python 3 和 libsodium：

```powershell
cmake -S . -B build -G Ninja -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure

# 输出目录必须不存在或为空；演示库必须是新路径。
python sample-data/simulation/generate.py --output build/simulation
.\build\server\media_demo_import.exe --demo-db build\media-demo.sqlite --fixture build\simulation\algorithm_fixture.json
.\build\server\media_server.exe --db build\media-demo.sqlite --listen 127.0.0.1 --port 8765
```

模拟账号为 `sim_user_01` 至 `sim_user_20`，默认密码 `demo-only-123` 仅用于演示。01 有模拟历史，20 是没有历史的新用户。生成器使用固定历史时间范围，演示足迹时应选择覆盖 2026 年 8 月 17 日至 11 月 15 日零点（北京时间，不含结束时刻）的时间范围；只查当前周不一定能看到导入历史。库及其后续用户交互数据不要当作真实用户数据。

上面的服务器监听地址只能让本机连接。两台电脑联调时，在服务器机器上用 `ipconfig` 查询其局域网 IPv4 地址，并把 `--listen` 与 Qt 连接地址设成该地址；端口以实际设置为准。不要把演示数据库覆盖到已有业务库。导入细节和重跑规则见 `sample-data/simulation/README.md`。

DeepSeek 的 Key 只在服务器进程环境设置：

```powershell
$env:MEDIA_SERVER_DEEPSEEK_API_KEY = '<在服务器终端填写自己的有效 Key>'
.\build\server\media_server.exe --db build\media-demo.sqlite --listen 127.0.0.1 --port 8765
```

不要把真实 Key 写进代码、文档、截图或提交。是否能调用真实模型应另行实测；空结果和规则回退要按接口返回展示。

## 二、Qt 接口与调用顺序

头文件 `include/online_care_client.h` 的 `OnlineCareClient` 同时实现 `WarehouseService` 和 `CareService`。一个登录会话使用同一个客户端实例；页面持有引用时要保证客户端生命周期长于页面。客户端方法及回调在对象所属线程执行，失败时回调可能同步触发。调用者须检查 `result.status.ok()` 和 `result.value`，不要把失败显示成空数据。

1. `connectToServer(host, port, callback)` 成功后调用 `login(username, password, callback)`。账号身份由服务器会话决定，调用方不传用户 ID。
2. `listMyWarehouses`、`listPublicWarehouses`、`getWarehouse` 提供仓库和条目；增删通过 `addItem`、`removeItem`，权限由服务器检查。
3. 用户主动点击媒体时用 `getResource({warehouseId, itemId}, callback)` 获取 `ResourceSummary`，再把其中 `resource` 放入 `BehaviorEvent`，调用 `recordBehavior(event, callback)`。`eventId` 每次新点击生成新值，同一事件重试保留原 ID 和原内容。`BehaviorAction::View` 的 `query` 为空。刷新、切页和自动选中不记录。
4. `getFootprint({range, comparison}, callback)` 返回 `FootprintReport`；`range` 是 UTC Unix 秒半开区间 `[beginUtc, endUtc)`，`comparison` 可为空。显示 `current`、`message` 和 `containsSyntheticData`。人物标识关联尚未接入，人物统计为空并由返回信息说明。
5. 智能仓库推荐调用 `recommendResources(WarehouseRecommendationRequest, callback)`，展示按序返回的 `items`、每条 `reason`、整体 `message` 和 `fallback`；成功但无匹配是正常空结果。若允许取消，用返回句柄调用 `cancelRecommendation`。不要调用尚未接线的 `recommendTemporal` 冒充已有功能。
6. 登出、断线、切账号或切仓库时，清除旧显示，使用页面代次或请求标记丢弃旧回调。使用 `OutcomeUnknown` 提示写入结果未知；重试同一浏览事件时保留事件 ID。

现阶段 `searchPeople`、关注、资讯、热榜、反馈及 `recommendTemporal` 等未接服务端的 CareService 方法会明确返回错误，不应展示为已实现。

## 三、自动联调检查

完成上面的分支整合并构建后，可运行：

```powershell
python sample-data/simulation/demo_end_to_end_check.py . build
```

该脚本只使用临时文件和本机临时端口：生成样例、创建独立演示库、启动真实服务器，以 01 和 20 登录并查询足迹，与生成器的原始事件数对照。它不修改现有数据库，也不替代 Qt 手动演示、真实 DeepSeek API 和双电脑联调。
