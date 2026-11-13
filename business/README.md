# 主逻辑使用说明

需要 C++17、CMake 和 Qt 6 Core/Test。在项目根目录配置并运行：

```powershell
. .\business\dev-env.ps1  # 默认使用 C:\msys64\ucrt64；只影响当前终端
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Qt 和编译器的安装路径由开发环境提供；Windows 下 Qt 的 DLL 必须在 PATH 中。CMake 构建 `media_business` 库以及检索库。Qt 界面程序可链接 `media_business`，包含 `medialibrary.h`。

账户服务还需要 Qt 6 Sql、SQLite 驱动和 libsodium（通过 pkg-config 查找）。MSYS2 UCRT64 下可安装 `mingw-w64-ucrt-x86_64-libsodium`、`mingw-w64-ucrt-x86_64-pkgconf` 和 `mingw-w64-ucrt-x86_64-qt6-base`。发布程序时须同时部署 Qt SQL SQLite 驱动和 libsodium 运行库。

`AccountHistoryServiceImpl` 位于 `include/account_history_service.h`，由应用创建并传入 CSV 路径；可调用 `initializeDefaultDatabase()` 在 `QStandardPaths::AppLocalDataLocation` 初始化默认数据库，或调用 `initializeDatabase(dbPath)` 指定路径，再调用 `needsInitialAdmin()` 等共享接口。接口必须从所属线程调用；在一次媒体操作中，`MediaMutationResult.persisted` 表示 CSV 是否已经改变，不能把索引同步失败当作可盲目重试的普通失败。

**集成边界：**本次实现的账户服务会检查当前会话和管理员角色；现有 Qt 界面仍调用旧版 `MediaLibrary`，还没有接入账户或历史接口。Qt 接入时必须把新增、删除和主动点击改为调用账户服务，并在保存成功后同步其持有的检索索引；接入之前不能认为现有界面已经具备角色限制。`MediaLibrary` 和 `CsvMediaRepository` 是底层数据组件，旧入口本身不检查登录角色。

当前使用 SQLite `user_version=1`。若检测到包含 `users` 或 `media_history` 的未版本化数据库，将拒绝修改并提示先迁移，不自动覆盖旧数据；迁移方案需结合实际旧表确认。
若 MSYS2 安装位置不同，可用 `. .\business\dev-env.ps1 -MsysRoot D:\msys64`。脚本默认将当前终端 TEMP/TMP 指向 `C:\tmp`，以避免某些 g++ 版本无法处理中文用户临时目录；其他平台应配置自己的 Qt 6 工具链。

```cpp
MediaLibrary library(csvPath, configuredPassword);
auto result = library.load();  // csvPath 必须指向已有、格式正确的 CSV
if (!result.success) { /* 显示 result.error */ }
if (!library.authenticate(inputPassword)) { /* 不进入媒体库 */ }
auto types = library.fileTypes();
auto ordered = library.records("MP4"); // 按发行日期从新到旧
MediaRecord detail;
bool found = library.find(id, detail);
auto ids = library.search(query);
result = library.addNew(record); // 自动分配新 ID；成功后 record.id 可用于界面刷新
result = library.remove(id);  // result.success 时界面刷新
```

口令由主程序传入，空口令永远验证失败；代码不提供默认密码。此接口只提供课程演示所需的口令比较，不提供密码安全存储、用户管理或防暴力破解。Qt 界面负责阻止验证失败后进入，不要将未通过验证的调用视为已授权。

CSV 使用 UTF-8，支持标准双引号转义和字段内换行；`actors`、`awards` 必须是 JSON 字符串数组。支持原九列表头并将缺失的 `category` 视为空，保存时升级为十列。加载失败时保留原有记录和索引。新增、删除先读原数据，用 `QSaveFile` 写临时文件并提交；文件保存成功后才更新检索索引。索引更新异常时尝试从已保存文件重建，错误信息会说明文件是否已修改；主程序应根据提示重新加载并刷新界面。

该模块不自动创建 CSV 或修改样例数据。联调时请先将 `sample-data/data/media_records.csv` 复制到单独的可写位置，并把副本路径传入。修改前会检查文件内容是否已变化，但该检查不提供跨进程锁；不要让多个进程同时修改同一文件。日期相同时按 ID 升序排列。

新增与导入均使用 `MediaLibrary::supportedFileTypes()` 支持列表（如 MP4、MP3、WAV），必须使用大写扩展名。界面可用该列表提供选项；不支持的 CSV 类型会导致加载失败，需由用户先更正源数据。

新记录请调用 `addNew(record)`，它在保存前分配 `M` 加至少六位数字的 ID；旧记录、包括 UUID，不会改写。旧的 `MediaLibrary::add` 仍可调用，也会自动分配新 ID，但不会返回分配的 ID，新代码应使用 `addNew`。`CsvMediaRepository::add` 是保留既有 ID 的底层接口，不应用于界面新增。编号高水位保存在 CSV 同目录的 `<CSV文件名>.id-sequence`，首次加载时根据现存 `M` 编号初始化，以后删记录不会降低高水位。预留成功后如果保存失败，编号会留空，不再复用；损坏或不可写的编号文件会返回错误，拒绝分配。迁移或备份时必须同时保留 CSV 和编号状态文件；第一次启用前已经删除的编号无法从现存 CSV 中推断。此机制不允许多个进程同时修改同一媒体库。
