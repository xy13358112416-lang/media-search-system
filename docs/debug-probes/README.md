# Qt 切页与无效输入卡顿：复现、测试和性能证据

对应 `DEBUG_LOG.md` 的 DBG-001 和 DBG-011。所有测试使用 CSV 的独立临时副本，不改样例或用户运行库。

## 自动回归

`frontend/tests/window_tests.cpp` 直接包含当前 `frontend/main.cpp`，触发真实页签、查询、保存和删除回调。辅助头中的派生类只计数，仍执行实际 `MediaLibrary` 方法和 Qt 尺寸计算；没有修改生产类接口。

- 30 / 530 条数据下重复切页，检查尺寸扫描次数有固定上界，避免逐格扫描引起的数量级放大；不把机器耗时作为 CTest 通过条件。
- 等数量但不同内容的查询、零结果、清空条件、类型筛选、推荐及详情同步。
- 非法类型保留表单，不调用新增或重新加载，不改表格、媒体 CSV 或编号状态文件。
- 原表单修改为带空白的小写合法类型后可以保存，并正确更新检索；删除后及时移除。
- 外部修改 CSV 后保存失败，原有重载恢复仍生效。

`DBG-001-011-regressions-before.txt` 是生产修改前的失败结果；`DBG-001-011-regressions-after.txt` 是修改后的完整结果。前者只运行两项目标回归，后者运行全部界面测试，因此两份总用例数不同。完整项目另由 CTest 执行 7 个测试组。

首次编写显示正确性测试时，误选了样例中有重复的两个文件名。实际命中 2 条，随后改用各只存在 1 次的 `晨光.mp3` 和 `归途.wav` 验证“数量相同但内容不同”，未改变生产检索行为。

## 性能探针

`frontend/tests/window_benchmark.cpp` 是显式构建目标 `media_frontend_benchmark`，不参与默认构建或 CTest。每个数据规模测量五次媒体库→检索、检索→媒体库和非法类型保存。参数为种子 CSV 和可选的日志输出路径。

记录同步耗时和处理布局事件后的总耗时。`finishLayout()` 处理已投递事件和表头尺寸请求，避免只测“将布局推迟”之前的部分。非法保存的 `prompt_ms` 为点击保存到进入错误对话框事件循环的时间，不包含人工阅读等待。`addCalls` / `loadCalls` 是界面调用业务入口的次数。

`before-30` / `before-530` 为修改前版本；`layout-only-530` 为只应用批量布局修改、尚未前置类型白名单的中间结果；`after-30` / `after-530` 为完整修复。原始 `DBG-011-30-records.log`、`DBG-011-530-records.log` 是更早的单轮诊断，探针和构建不同，不能与本轮五次测量混算中位数。

## 本机重跑命令（PowerShell）

以下使用已验证配套的 Qt 6.8.3 / MinGW 13.1，避免混用其他 MinGW：

```powershell
$taskRepo = 'D:/code/cpp_shangji/media-search-system'
$taskSdk = 'D:/code/cpp_shangji/.qt-sdk'
$env:PATH = "$taskSdk/Tools/mingw1310_64/bin;$taskSdk/6.8.3/mingw_64/bin;D:/mingw64/bin;$env:PATH"
$env:TEMP = "$taskSdk/test-temp"
$env:TMP = $env:TEMP

cmake -S $taskRepo -B "$taskSdk/build-media" -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_CXX_COMPILER=$taskSdk/Tools/mingw1310_64/bin/g++.exe" "-DCMAKE_PREFIX_PATH=$taskSdk/6.8.3/mingw_64"
cmake --build "$taskSdk/build-media"
ctest --test-dir "$taskSdk/build-media" --output-on-failure

cmake --build "$taskSdk/build-media" --target media_frontend_benchmark
$env:QT_QPA_PLATFORM = 'offscreen'
& "$taskSdk/build-media/frontend/media_frontend_benchmark.exe" "$taskRepo/sample-data/expanded/media_records_expanded.csv" "$taskSdk/test-temp/frontend-repeat-530.log"
Get-Content "$taskSdk/test-temp/frontend-repeat-530.log"
```

`BUILD_TESTING` 默认为开；关闭后不创建新增的界面测试与探针目标。独立运行 QtTest 时，可用 `-o 路径,txt` 保存完整测试输出。

## 测量限制

这是 Windows 上真实 Qt 控件的 offscreen 自动化测试，没有人工鼠标输入和桌面绘制截图。offscreen 输出的字体目录缺失及 `propagateSizeHints` 提示保留在测试日志中，不等于业务错误。正常窗口和不同字体、系统负载的绝对时间可能不同。正式演示前仍应在正常 Windows 窗口人工复核切页、列宽和保存提示。
