#pragma once
#include "account_history_contract.h"
#include "medialibrary.h"
#include <QSqlDatabase>
#include <memory>

// 应用持有此服务；所有有副作用的媒体操作通过它执行权限检查。
class AccountHistoryServiceImpl final : public account_contract::AccountHistoryService {
public:
    explicit AccountHistoryServiceImpl(QString csvPath = {});
    // 复用调用方的媒体库与索引；library 必须比账户服务活得更久。
    explicit AccountHistoryServiceImpl(MediaLibrary& library);
    ~AccountHistoryServiceImpl() override;
    static QString defaultDatabasePath(); // 应用本地数据目录内的默认数据库路径。
    account_contract::Status initializeDefaultDatabase(); // 创建本地数据目录并初始化默认数据库。
    account_contract::Status initializeDatabase(const QString& databasePath) override;
    account_contract::Result<bool> needsInitialAdmin() override;
    account_contract::Status createInitialAdmin(const QString& username, const QString& password) override;
    account_contract::Status registerUser(const QString& username, const QString& password) override;
    account_contract::Result<account_contract::UserIdentity> login(const QString& username, const QString& password) override;
    std::optional<account_contract::UserIdentity> currentUser() const override;
    void logout() override;
    account_contract::MediaMutationResult addMedia(const MediaRecord& record) override;
    account_contract::MediaMutationResult removeMedia(const QString& mediaId) override;
    account_contract::Status recordMediaView(qint64 userId, const QString& mediaId) override;
    account_contract::Result<QList<account_contract::MediaHistoryEntry>> loadMediaHistory(qint64 userId) override;
private:
    account_contract::Status createUser(const QString& username, const QString& password, bool admin);
    account_contract::Status checkUser(qint64 userId) const;
    account_contract::Status checkAdmin() const;
    account_contract::Status ensureLibrary();
    MediaLibrary* activeLibrary() const; // 返回当前使用的媒体库，不转移所有权。
    QString connectionName_;
    QSqlDatabase db_;
    QString csvPath_;
    std::unique_ptr<MediaLibrary> library_;
    MediaLibrary* sharedLibrary_ = nullptr; // 非拥有指针，适用于与界面共享快照。
    std::optional<account_contract::UserIdentity> session_;
};
