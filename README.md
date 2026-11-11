# 多媒体文件管理及检索系统

## Qt 前端

需要 C++17、CMake、Ninja 和 Qt 6（Core、Test、Widgets）。Windows 上可用业务模块提供的环境脚本配置工具链。使用其他 Qt 套件时，确保 CMake 能找到 Qt，并且 Qt 的 DLL 位于 PATH 中。

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

打开「检索」，可组合输入导演、演员和文件类型。使用原始样例时，输入「林川」「陈明」「MP4」应命中两条。结果下方按实际索引执行顺序显示各步的单项命中数和剩余候选数。「清空条件」会显示全部记录。
