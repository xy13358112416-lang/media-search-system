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
struct UserIdentity {
    qint64 userId = 0;
    QString username;
    UserRole role = UserRole::User;
};

struct MediaHistoryEntry {
    QString mediaId;
    qint64 viewCount = 0;
    qint64 lastViewedAt = 0; // UTC Unix 秒。
};

// CSV 已提交但索引更新失败时，persisted 为 true，调用方不得盲目重试。
struct MediaMutationResult {
    Status status;
    bool persisted = false;
    QString mediaId; // 已持久化时为实际新增或删除的 ID。
};

class AccountHistoryService {
public:
    virtual ~AccountHistoryService() = default;

    // 打开指定 SQLite 文件并初始化/升级结构；失败返回 DatabaseError。
    virtual Status initializeDatabase(const QString& databasePath) = 0;
    // 用户表为空时返回 true；未初始化或数据库故障返回错误。
    virtual Result<bool> needsInitialAdmin() = 0;
    // 仅用户表为空时在同一写事务中创建管理员；失败不产生部分账户。
    virtual Status createInitialAdmin(const QString& username, const QString& password) = 0;
    // 注册普通用户；不会接受调用方传入角色。
    virtual Status registerUser(const QString& username, const QString& password) = 0;
    // 成功后替换当前会话；失败后无登录会话，不区分用户名或密码错误。
    virtual Result<UserIdentity> login(const QString& username, const QString& password) = 0;
    // 未登录返回空；不包含密码哈希。
    virtual std::optional<UserIdentity> currentUser() const = 0;
    virtual void logout() = 0;

    // 从会话获取角色；未登录或非管理员不得写入 CSV。
    // 成功后更新检索索引；索引失败时返回 IndexSyncError 和 persisted=true。
    virtual MediaMutationResult addMedia(const MediaRecord& record) = 0;
    virtual MediaMutationResult removeMedia(const QString& mediaId) = 0;

    // userId 必须等于当前会话用户，且 mediaId 必须存在；成功时原子累加历史。
    virtual Status recordMediaView(qint64 userId, const QString& mediaId) = 0;
    // 仅允许查询当前用户；无记录返回成功空列表；数据库故障返回错误。
    virtual Result<QList<MediaHistoryEntry>> loadMediaHistory(qint64 userId) = 0;
};
} // namespace account_contract
