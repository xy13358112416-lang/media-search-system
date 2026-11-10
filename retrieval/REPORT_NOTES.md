# 检索开发记录与报告素材

检索模块接收主逻辑加载的 `QList<MediaRecord>`。查询条件为导演、演员、文件扩展名，填写的条件同时生效。导演和演员按完整姓名匹配，扩展名忽略大小写，查询和记录字段都先去掉首尾空白。输出为去重且按 ID 升序排列的 `QStringList`。

## 开发和测试顺序

| 阶段 | 本阶段先验证什么 | 实现与验收 |
|---|---|---|
| 1. 逐条扫描 | 单条件、组合条件、无结果、空条件能得到手算的 ID。新增测试最初能编译到链接阶段，缺少 `rebuild` 和 `search` 的定义。 | 实现记录加载、字段规范化和逐条匹配，`search_scan` 通过。 |
| 2. 固定顺序索引 | 固定顺序检索要和扫描一致，新增、删除和重建后结果要同步。新增测试最初链接不到索引查询和增删方法。 | 建立导演、演员和类型到有序 ID 列表的映射，`search_scan`、`search_index`、`search_updates` 均通过。 |
| 3. 短列表优先 | 优化前稀有演员未排到首位，缺失条件查询执行了两步。两项断言均失败。 | 按命中数升序执行，缺失条件排首位并立即得到空结果。`search_optimization` 的两项断言随后通过。 |

测试还覆盖同名记录的独立 ID、重复演员、大小写和空白、重复或空 ID、删除未知 ID。重建先构造新索引，校验失败时保留已有数据。主程序在持久化成功后才调用 `addRecord` 或 `removeRecord`，界面刷新时再用最新索引查询。

## 复现实验

用 `MediaSearch-Qt.code-workspace` 打开 VS Code，在仓库根目录的新终端运行以下命令。这里使用独立构建目录，便于将检索演示与根目录的练习程序分开。

```powershell
$searchBuild = '../.qt-sdk/build-retrieval'
cmake -S retrieval -B $searchBuild -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build $searchBuild
ctest --test-dir $searchBuild --output-on-failure
& "$searchBuild/media_search_demo.exe"
```

演示程序用手写的 `MediaRecord`，运行三种查询策略，展示第一步和后续步骤的候选数，再删除一条命中记录后重查。它可以在没有 CSV 和界面的情况下验证检索。

```powershell
$csv = & "$searchBuild/media_search_benchmark.exe"
if ($LASTEXITCODE -ne 0) { throw '检索对照实验未通过' }
$csv | Set-Content -Encoding utf8 "$searchBuild/search-benchmark.csv"
```

性能实验使用固定种子 `20261110` 生成 1 千、1 万和 10 万条记录。分别执行稀有演员、稀有类型、热门条件、无结果四类查询。每种策略先预热三次，再轮换顺序测量 31 次。每次核对 ID 列表，核对和输出都在计时区间外。`p50_us` 和 `p95_us` 分别是查询耗时的中位数和第 95 百分位，单位为微秒。`initial_candidates` 是索引策略处理第一个条件后的候选数，扫描留空。`index_build_ms` 是整个批次的索引构建耗时，在相同规模的行中重复显示。

这些是合成数据上的对照。组员采集的样例可用于演示实际字段和 CSV 联调。报告可以把小规模验证、规模实验和真实数据演示分开写。

## 本次实测记录

在 Windows 11、Qt 6.8.3、MinGW 13.1、Release 下验证。4 组 CTest 测试均通过，包含 7 个实际测试用例。演示中三种策略均查到 `M000001`，短列表优先使初始候选由 3 条降至 1 条。删除后查到 0 条。

规模实验生成了 36 行结果。下面是 10 万条记录时的部分中位耗时，单位为微秒。

| 查询 | 逐条扫描 | 固定顺序索引 | 短列表优先 | 固定顺序 → 短列表优先的初始候选 |
|---|---:|---:|---:|---:|
| 稀有演员 | 9169.1 | 2458.6 | 2451.3 | 50163 → 101 |
| 稀有类型 | 12693.2 | 4785.7 | 2778.8 | 50163 → 101 |
| 热门条件 | 27571.0 | 7676.5 | 7428.9 | 50163 → 50163 |
| 无结果 | 9206.7 | 5.5 | 1.6 | 50163 → 0 |

本次稀有类型查询的中位耗时下降约 42%。稀有演员的初始候选也大幅下降，中位耗时却几乎持平，因为求交仍需读取较长的列表。无结果查询少执行一步。构建 10 万条记录的索引耗时约 162 毫秒。原始 CSV 还保留了 p95，适合和中位数一起讨论波动。以上数字来自一次本机实验，报告使用时应保留数据规模和测量条件。

## 主程序接入

主项目可以在 CMake 中添加 `add_subdirectory(retrieval)`，再把应用目标链接到 `media_search`。此时检索库对外提供 `include/mediasearch.h` 和 Qt Core 依赖。

```cpp
MediaSearch search;
search.rebuild(records);

SearchTrace trace;
const QStringList ids = search.search(query, SearchStrategy::SmallestListFirst, &trace);
```

主逻辑根据返回的 ID 取完整记录，Qt 用 `trace.steps` 展示各步命中数和剩余候选。平时直接调用 `search.search(query)` 即可，它默认使用短列表优先。
