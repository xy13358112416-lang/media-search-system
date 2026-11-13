# 多媒体文件管理及检索系统

## Qt 前端

需要 C++17、CMake、Ninja 和 Qt 6（Core、Test、Widgets、Sql），并通过 pkg-config 查找 libsodium。Windows 上可用业务模块提供的环境脚本配置工具链。使用其他 Qt 套件时，确保 CMake 能找到 Qt，并且 Qt 的 DLL 位于 PATH 中。

```powershell
. .\business\dev-env.ps1
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
& .\build\frontend\media_library_qt.exe
```

首次运行创建管理员，随后用用户名和密码登录。可从登录页注册普通账户。账户和浏览历史保存在应用本地数据目录的 `accounts.sqlite`。首次运行时会把可执行文件同目录的 `media_records_sample.csv` 复制为可修改的 `media_records.csv`。之后启动继续使用这份文件，重新构建只更新样例原件。也可以在启动时用 `--csv "C:\path\media_records.csv"` 指定其他数据文件。

验收时可先输入错误密码，再输入正确密码。登录后切换「全部类型」和「MP4」，核对列表数量和发行日期降序，再点选一条记录核对详情。

用管理员账户测试增删时，先新增一条记录并确认列表和详情已更新，再重启程序检查记录仍在。选中记录后点「删除」，先取消一次，再确认删除，最后重启检查该记录已删除。操作只修改 CSV 中的媒体记录。

打开「检索」，可组合输入文件名关键词、导演、演员和文件类型。输入文件名的一部分时会显示匹配的文件名，选中建议可直接查询。使用原始样例时，输入「林川」「陈明」「MP4」应命中两条。结果下方按实际索引执行顺序显示各步的单项命中数和剩余候选数。「清空条件」会显示全部记录。

「推荐」的类别和文件类型都可选择自动、不限或指定偏好。自动按当前账户的浏览历史推断，指定值参与加分。类别匹配加 2 分，文件类型匹配加 1 分，浏览次数除以全表最大浏览次数作为热度分，最多加 1 分。全表浏览次数为零时热度为零，同分按 ID 升序，最多展示 5 条。「检索」中的「性能对照」展示已记录的 10 万条合成数据实验。

## Windows 演示打包

Release 构建后，使用对应 Qt 套件的 bin 目录运行 `frontend/package.ps1`。指定一个尚不存在的输出目录。如果构建所用的 `libsodium.dll` 不在 PATH 中，请通过 `-SodiumDll` 指定它的路径。

```powershell
.\frontend\package.ps1 -BuildDirectory .\build -QtBin C:\Qt\6.x\mingw_64\bin -OutputDirectory .\media-library-demo
```

将输出目录整体压缩。解压后双击 `media_library_qt.exe`。首次启动创建管理员，之后直接登录。
