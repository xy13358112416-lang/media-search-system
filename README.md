# 多媒体文件管理及检索系统

## Qt 前端

需要 C++17、CMake、Ninja 和 Qt 6（Core、Test、Widgets、Sql（含 QSQLITE 驱动））。Windows 上可用业务模块提供的环境脚本配置工具链。使用其他 Qt 套件时，确保 CMake 能找到 Qt，并且 Qt 的 DLL 位于 PATH 中。

```powershell
. .\business\dev-env.ps1
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
$env:MEDIA_LIBRARY_PASSWORD = Read-Host '设置本次演示口令'
& .\build\frontend\media_library_qt.exe
```

输入刚设置的口令即可打开媒体库。首次运行时会把可执行文件同目录的 `media_records_sample.csv` 复制为可修改的 `media_records.csv`。之后启动继续使用这份文件，重新构建只更新样例原件。也可以在启动时用 `--csv "C:\path\media_records.csv"` 指定其他数据文件。

验收时可先输入错误口令，再输入正确口令。登录后切换「全部类型」和「MP4」，核对列表数量和发行日期降序，再点选一条记录核对详情。

测试增删时，先新增一条记录并确认列表和详情已更新，再重启程序检查记录仍在。选中记录后点「删除」，先取消一次，再确认删除，最后重启检查该记录已删除。操作只修改 CSV 中的媒体记录。

打开「检索」，可组合输入文件名关键词、导演、演员和文件类型。输入文件名的一部分时会显示匹配的文件名，选中建议可直接查询。使用原始样例时，输入「林川」「陈明」「MP4」应命中两条。结果下方按实际索引执行顺序显示各步的单项命中数和剩余候选数。「清空条件」会显示全部记录。

「推荐」可选择类别和文件类型。优先显示匹配偏好项较多的记录，再按浏览次数从高到低排序，最多展示 5 条。「检索」中的「性能对照」展示已记录的 10 万条合成数据实验。

## Windows 演示打包

Release 构建后，使用对应 Qt 套件的 bin 目录运行 `frontend/package.ps1`。指定一个尚不存在的输出目录。

```powershell
.\frontend\package.ps1 -BuildDirectory .\build -QtBin C:\Qt\6.x\mingw_64\bin -OutputDirectory .\media-library-demo
```

将输出目录整体压缩。解压后双击 `media_library_qt.exe`，在图形窗口设置本次演示口令，再用同一口令登录。两个口令框都支持中文输入，均可勾选「显示口令」核对。若启动环境已配置 `MEDIA_LIBRARY_PASSWORD`，程序直接进入登录窗口。

## 多用户点击榜

登录时填写固定的用户标识。标识忽略首尾空格和大小写，用于区分点击人数。口令仍是媒体库共用口令。点击列表中的记录后，CSV 中的累计浏览次数增加 1，并保存点击明细。可在「媒体库 → 点击榜」查看最近 7 天的前 10 条。生成推荐仍使用累计浏览次数。榜单按不同用户数降序，再按点击次数降序。筛选、检索和自动选中记录不计为点击。

行为数据保存在 CSV 同目录的 `<CSV 文件名>.clicks.sqlite`，统计范围为使用这份数据库的演示会话。
