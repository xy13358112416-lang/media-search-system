// >>> MAIN-DEFENSE | 主逻辑头文件导读
// 共享接口/数据契约：上层通过这里的类型和方法与主逻辑对接。声明不等于执行，实际实现见对应cpp。
// 阅读顺序：输入数据结构 → 返回状态/可选数据 → 方法声明 → 私有成员。引用/指针保存依赖，必须确认由谁拥有及何时释放。
// <<< MAIN-DEFENSE
#pragma once

#include "mediarecord.h"
#include <QList>
#include <QString>
#include <QtGlobal>
#include <optional>

// 账户与历史共享接口声明；数据库和账户功能由后续实现提供。
namespace account_contract {

enum class ErrorCode {
    None, InvalidArgument, NotInitialized, InitialAdminRequired,
    InitialAdminAlreadyExists, UsernameTaken, AuthenticationFailed,
    NotAuthenticated, PermissionDenied, MediaNotFound,
    DatabaseError, PasswordHashError, StorageError, IndexSyncError
};

// >>> MAIN-DEFENSE | 接口结构｜Status
// 这是数据包：检查字段含义、默认值和optional缺失状态。
// <<< MAIN-DEFENSE
struct Status {
    ErrorCode code = ErrorCode::None;
    QString message; // 非敏感的可展示错误；成功时为空。
    bool ok() const { return code == ErrorCode::None; }
};

template <typename T> struct Result {
    Status status;
    std::optional<T> value; // 成功时有值；失败时为空。
};

enum class UserRole { User, Admin };
// >>> MAIN-DEFENSE | 接口结构｜UserIdentity
// 这是数据包：检查字段含义、默认值和optional缺失状态。
// <<< MAIN-DEFENSE
struct UserIdentity {
    qint64 userId = 0;
    QString username;
    UserRole role = UserRole::User;
};

// >>> MAIN-DEFENSE | 接口结构｜MediaHistoryEntry
// 这是数据包：检查字段含义、默认值和optional缺失状态。
// <<< MAIN-DEFENSE
struct MediaHistoryEntry {
    QString mediaId;
    qint64 viewCount = 0;
    qint64 lastViewedAt = 0; // UTC Unix 秒。
};

// CSV 已提交但索引更新失败时，persisted 为 true，调用方不得盲目重试。
// >>> MAIN-DEFENSE | 接口结构｜MediaMutationResult
// 这是数据包：检查字段含义、默认值和optional缺失状态。
// <<< MAIN-DEFENSE
struct MediaMutationResult {
    Status status;
    bool persisted = false;
    QString mediaId; // 已持久化时为实际新增或删除的 ID。
};

// >>> MAIN-DEFENSE | 接口结构｜AccountHistoryService
// 这是服务对象：public为调用入口，private保存内部状态；Qt或其他模块通过接口调用，业务权限在实现中校验。
// <<< MAIN-DEFENSE
class AccountHistoryService {
public:
    // >>> MAIN-DEFENSE | 方法声明｜AccountHistoryService
    // 本行约定调用参数和返回类型；具体分块处理在对应cpp，调用者须遵守已有注释中的权限与生命周期约定。
    // <<< MAIN-DEFENSE
    virtual ~AccountHistoryService() = default;

    // 打开指定 SQLite 文件并初始化/升级结构；失败返回 DatabaseError。
    // >>> MAIN-DEFENSE | 方法声明｜initializeDatabase
    // 清旧会话/连接，初始化密码组件，打开SQLite并核对或创建表结构，配置外键和等待超时。
    // <<< MAIN-DEFENSE
    virtual Status initializeDatabase(const QString& databasePath) = 0;
    // 用户表为空时返回 true；未初始化或数据库故障返回错误。
    // >>> MAIN-DEFENSE | 方法声明｜needsInitialAdmin
    // 查询单机库是否还需要首次管理员；不是把任意注册用户提升权限。
    // <<< MAIN-DEFENSE
    virtual Result<bool> needsInitialAdmin() = 0;
    // 仅用户表为空时在同一写事务中创建管理员；失败不产生部分账户。
    // >>> MAIN-DEFENSE | 方法声明｜createInitialAdmin
    // 只在允许首次建管理员的状态调用统一创建流程。
    // <<< MAIN-DEFENSE
    virtual Status createInitialAdmin(const QString& username, const QString& password) = 0;
    // 注册普通用户；不会接受调用方传入角色。
    // >>> MAIN-DEFENSE | 方法声明｜registerUser
    // 通过统一创建流程建立普通用户，不允许注册入口自己选择管理员角色。
    // <<< MAIN-DEFENSE
    virtual Status registerUser(const QString& username, const QString& password) = 0;
    // 成功后替换当前会话；失败后无登录会话，不区分用户名或密码错误。
    // >>> MAIN-DEFENSE | 方法声明｜login
    // 按规范化用户名读密码哈希，验证明文输入；成功后建立当前会话，失败不能保留旧身份。
    // <<< MAIN-DEFENSE
    virtual Result<UserIdentity> login(const QString& username, const QString& password) = 0;
    // 未登录返回空；不包含密码哈希。
    // >>> MAIN-DEFENSE | 方法声明｜currentUser
    // 读取当前会话身份；用于权限/个人历史校验，不是从界面显示文字判断用户。
    // <<< MAIN-DEFENSE
    virtual std::optional<UserIdentity> currentUser() const = 0;
    // >>> MAIN-DEFENSE | 方法声明｜logout
    // 清当前登录状态，使后续受保护操作不能沿用退出前身份。
    // <<< MAIN-DEFENSE
    virtual void logout() = 0;

    // 从会话获取角色；未登录或非管理员不得写入 CSV。
    // 成功后更新检索索引；索引失败时返回 IndexSyncError 和 persisted=true。
    // >>> MAIN-DEFENSE | 方法声明｜addMedia
    // 先业务层管理员校验，再新增媒体，保留保存与同步结果供Qt判断。
    // <<< MAIN-DEFENSE
    virtual MediaMutationResult addMedia(const MediaRecord& record) = 0;
    // >>> MAIN-DEFENSE | 方法声明｜removeMedia
    // 先业务层管理员校验，再删除媒体，并传递是否已经持久化，避免盲目重试。
    // <<< MAIN-DEFENSE
    virtual MediaMutationResult removeMedia(const QString& mediaId) = 0;

    // userId 必须等于当前会话用户，且 mediaId 必须存在；成功时原子累加历史。
    // >>> MAIN-DEFENSE | 方法声明｜recordMediaView
    // 校验本人身份和媒体引用，更新个人浏览次数与最后浏览时间，不记录自动选中。
    // <<< MAIN-DEFENSE
    virtual Status recordMediaView(qint64 userId, const QString& mediaId) = 0;
    // 仅允许查询当前用户；无记录返回成功空列表；数据库故障返回错误。
    // >>> MAIN-DEFENSE | 方法声明｜loadMediaHistory
    // 读取当前用户的浏览历史，数据库错误与正常空历史分开返回。
    // <<< MAIN-DEFENSE
    virtual Result<QList<MediaHistoryEntry>> loadMediaHistory(qint64 userId) = 0;
};
} // namespace account_contract
