# 账户、权限与浏览历史：三方接口约定

本文件及 `include/account_history_contract.h` 记录共享接口；**尚未实现账户、SQLite、权限、密码哈希或浏览历史**。后续 PR 需提交实现和行为测试。

## 职责和文件

| 负责方 | 工作和建议文件 |
|---|---|
| 主逻辑（我们） | 已有草案 `include/account_history_contract.h`。确认后建议新增 `business/account_service.cpp`、`business/database_migrations.cpp`、`business/history_repository.cpp` 及相应头文件和 `business/account_history_tests.cpp`；调整 `business/CMakeLists.txt` 与 `include/medialibrary.h` / `business/medialibrary.cpp` 的业务入口。媒体依然使用 CSV。具体拆分可由实现阶段调整。 |
| Qt 同学 | 以现有 `frontend/main.cpp` 为接入位置，按需拆分界面文件：实现登录、注册、创建管理员，依据角色控制按钮；只在主动点击时调用历史接口；展示推荐结果。 |
| 推荐同学 | 以现有 `include/mediarecommendation.h`、`retrieval/mediarecommendation.cpp` 和对应测试为接入位置，确定 `PreferenceMode` / 请求和返回类型，根据 `loadMediaHistory(currentUser.userId)` 推断偏好并返回 ID 和原因；维护检索索引同步接口。 |

## 共享类型和错误约定

- `UserIdentity` 包含 `qint64 userId`、显示用户名和角色；不返回哈希或密码。
- `MediaHistoryEntry` 包含字符串 `mediaId`、非负 `viewCount` 和 UTC Unix 秒 `lastViewedAt`。一个用户与一个媒体只保留一条累计记录；不能据此推断最近七天的精确浏览次数。
- `Status` 包含 `ErrorCode` 和可展示的 `message`；`Result<T>` 在成功时有 `value`，失败时无值。错误码区分初始化、参数、重复用户、认证、权限、数据库和持久化失败。认证失败不区分用户名与密码错误。
- 首次管理员创建和用户注册不应只依赖 UI 判断：业务层处理空用户表检查、唯一性和事务；普通注册永远只创建普通用户。用户名显示值与 NFC、去首尾空格、大小写折叠后的唯一键分开。密码原样按 UTF-8 交给 libsodium Argon2id；不自写密码学算法。

## 建议表结构

```sql
CREATE TABLE users (
    user_id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT NOT NULL,
    username_key TEXT NOT NULL UNIQUE,
    password_hash TEXT NOT NULL,
    role TEXT NOT NULL CHECK (role IN ('user', 'admin')),
    created_at INTEGER NOT NULL
);
CREATE TABLE media_history (
    user_id INTEGER NOT NULL REFERENCES users(user_id),
    media_id TEXT NOT NULL,
    view_count INTEGER NOT NULL CHECK (view_count >= 0),
    last_viewed_at INTEGER NOT NULL,
    PRIMARY KEY (user_id, media_id)
);
```

每个 SQLite 连接启用外键检查。媒体记录存在 CSV 中，`media_id` 无法由 SQLite 外键校验，写入历史前由业务层确认；媒体删除后遗留的历史由推荐方按当前媒体库过滤。真实迁移脚本和数据库版本在接口确认后实现。

## 接口行为

1. `initializeDatabase(path)` 打开指定文件并建表/迁移；`needsInitialAdmin()` 查询是否需要创建管理员。路径由应用层确定，计划使用应用本地数据目录。
2. `createInitialAdmin` 只在用户表为空时成功；`registerUser` 创建普通用户；`login` 成功后保存当前会话并返回身份；失败后无登录会话。`logout` 清空会话。切换账号时，Qt 和推荐方各自清除旧结果。
3. `addMedia(record)` 与 `removeMedia(mediaId)` 从主逻辑保存的会话获取权限，不接受调用方提供角色。未登录或非管理员直接拒绝，CSV 不改变。**是否已写入 CSV**由 `MediaMutationResult.persisted` 表达；如果写入成功但索引同步失败，返回 `IndexSyncError`，`persisted=true`，调用方刷新/重载而不盲目重试。
4. `recordMediaView(userId, mediaId)` 只接受当前会话用户，确认媒体存在后原子累加。`loadMediaHistory(userId)` 只返回当前用户的历史；无历史是成功空列表，数据库故障是错误。历史中的媒体可能已从 CSV 删除，推荐方须忽略不再存在的 ID。
5. 三种偏好是推荐同学的接口：`Auto` 根据当前用户历史推断，`Any` 不限制，`Explicit` 使用指定值；两项独立。新用户无历史时采用全局热度回退，显式偏好仍然生效。该草案不修改推荐算法。

| 接口 | 输入与成功返回 | 主要失败情况 |
|---|---|---|
| `initializeDatabase` | 数据库路径；返回状态 | 空路径、无法打开或建表/迁移失败 |
| `needsInitialAdmin` | 无；返回是否需要首个管理员 | 未初始化、查询失败 |
| `createInitialAdmin` | 用户名和密码；返回状态 | 参数无效、用户表非空、哈希或事务失败 |
| `registerUser` | 用户名和密码；返回状态 | 尚需创建管理员、参数无效、用户名重复、哈希或数据库失败 |
| `login` | 用户名和密码；返回身份 | 参数无效、身份验证失败、数据库或哈希失败 |
| `currentUser` / `logout` | 无；分别返回可选身份 / 清空会话 | 无登录时 `currentUser` 为空 |
| `addMedia` / `removeMedia` | 记录 / 媒体 ID；返回状态、是否已持久化及 ID | 未登录、无权限、参数无效、媒体不存在、CSV 写入或索引同步失败 |
| `recordMediaView` | 当前用户 ID、媒体 ID；返回状态 | 未登录、用户不匹配、媒体不存在、数据库失败 |
| `loadMediaHistory` | 当前用户 ID；返回历史列表（可为空） | 未登录、用户不匹配、数据库失败 |

## 确认后实现时的验收

- 空库首次创建管理员；重复创建失败；普通注册不能指定管理员；重复用户名规范化后拒绝。
- 中英文口令注册、登录；错误口令失败；哈希含盐且不存明文；数据库错误可见。
- 未登录和普通用户新增/删除被拒且 CSV 不变；管理员成功操作后索引一致；模拟写入失败及“写入成功但索引失败”。
- 主动点击更新当前用户的次数和时间；错误用户、无效媒体及数据库失败均返回错误；切换账号不读取旧账号历史。
- 重启后用户、历史和权限仍正确；Qt 自动选中、刷新或切页不重复记为主动点击（需 Qt 同学配合）。

草案编译验证仅证明头文件与 C++17/Qt 兼容，不证明上述行为已实现。对外 PR 必须包含实现和对应行为测试，不能只提交空接口。
