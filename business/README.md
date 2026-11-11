# 主逻辑使用说明

需要 C++17、CMake 和 Qt 6 Core/Test。在项目根目录配置并运行：

```powershell
. .\business\dev-env.ps1  # 默认使用 C:\msys64\ucrt64；只影响当前终端
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Qt 和编译器的安装路径由开发环境提供；Windows 下 Qt 的 DLL 必须在 PATH 中。CMake 构建 `media_business` 库以及检索库。Qt 界面程序可链接 `media_business`，包含 `medialibrary.h`。
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
result = library.add(record); // result.success 时界面刷新
result = library.remove(id);  // result.success 时界面刷新
```

口令由主程序传入，空口令永远验证失败；代码不提供默认密码。此接口只提供课程演示所需的口令比较，不提供密码安全存储、用户管理或防暴力破解。Qt 界面负责阻止验证失败后进入，不要将未通过验证的调用视为已授权。

CSV 使用 UTF-8，支持标准双引号转义和字段内换行；`actors`、`awards` 必须是 JSON 字符串数组。支持原九列表头并将缺失的 `category` 视为空，保存时升级为十列。加载失败时保留原有记录和索引。新增、删除先读原数据，用 `QSaveFile` 写临时文件并提交；文件保存成功后才更新检索索引。索引更新异常时尝试从已保存文件重建，错误信息会说明文件是否已修改；主程序应根据提示重新加载并刷新界面。

该模块不自动创建 CSV 或修改样例数据。联调时请先将 `sample-data/data/media_records.csv` 复制到单独的可写位置，并把副本路径传入。应用的新增、删除和累计浏览次数写入共用文件锁。累计浏览次数在持锁后从磁盘读取并递增；外部编辑器不使用这把锁。日期相同时按 ID 升序排列。
