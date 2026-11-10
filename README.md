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

输入刚设置的口令即可打开媒体库。可执行文件默认读取同目录的样例 `media_records.csv`，由构建步骤复制。也可以在启动时用 `--csv "C:\path\media_records.csv"` 指定其他数据文件。

验收时可先输入错误口令，再输入正确口令。登录后切换「全部类型」和「MP4」，核对列表数量和发行日期降序，再点选一条记录核对详情。
