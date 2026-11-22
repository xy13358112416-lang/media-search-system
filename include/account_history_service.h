// >>> MAIN-DEFENSE | 主逻辑头文件导读
// 单机账户和浏览历史：初始化SQLite、首次管理员、普通注册、密码哈希验证及会话、管理员媒体增删。在线仓库身份由另一套network_users和socket会话管理，不混用这两种权限模型。
// 阅读顺序：输入数据结构 → 返回状态/可选数据 → 方法声明 → 私有成员。引用/指针保存依赖，必须确认由谁拥有及何时释放。
// <<< MAIN-DEFENSE
#pragma once
#include "account_history_contract.h"
#include "medialibrary.h"
#include <QSqlDatabase>
#include <memory>

// 应用持有此服务；所有有副作用的媒体操作通过它执行权限检查。
// >>> MAIN-DEFENSE | 接口结构｜AccountHistoryServiceImpl
// 这是服务对象：public为调用入口，private保存内部状态；Qt或其他模块通过接口调用，业务权限在实现中校验。
// <<< MAIN-DEFENSE
class AccountHistoryServiceImpl final : public account_contract::AccountHistoryService {
public:
    // >>> MAIN-DEFENSE | 方法声明｜AccountHistoryServiceImpl
    // 本行约定调用参数和返回类型；具体分块处理在对应cpp，调用者须遵守已有注释中的权限与生命周期约定。
    // <<< MAIN-DEFENSE
    explicit AccountHistoryServiceImpl(QString csvPath = {});
    // 复用调用方的媒体库与索引；library 必须比账户服务活得更久。
    // >>> MAIN-DEFENSE | 方法声明｜AccountHistoryServiceImpl
    // 本行约定调用参数和返回类型；具体分块处理在对应cpp，调用者须遵守已有注释中的权限与生命周期约定。
    // <<< MAIN-DEFENSE
    explicit AccountHistoryServiceImpl(MediaLibrary& library);
    // >>> MAIN-DEFENSE | 方法声明｜AccountHistoryServiceImpl
    // 本行约定调用参数和返回类型；具体分块处理在对应cpp，调用者须遵守已有注释中的权限与生命周期约定。
    // <<< MAIN-DEFENSE
    ~AccountHistoryServiceImpl() override;
    static QString defaultDatabasePath(); // 应用本地数据目录内的默认数据库路径。
    account_contract::Status initializeDefaultDatabase(); // 创建本地数据目录并初始化默认数据库。
    // >>> MAIN-DEFENSE | 方法声明｜initializeDatabase
    // 清旧会话/连接，初始化密码组件，打开SQLite并核对或创建表结构，配置外键和等待超时。
    // <<< MAIN-DEFENSE
    account_contract::Status initializeDatabase(const QString& databasePath) override;
    // >>> MAIN-DEFENSE | 方法声明｜needsInitialAdmin
    // 查询单机库是否还需要首次管理员；不是把任意注册用户提升权限。
    // <<< MAIN-DEFENSE
    account_contract::Result<bool> needsInitialAdmin() override;
    // >>> MAIN-DEFENSE | 方法声明｜createInitialAdmin
    // 只在允许首次建管理员的状态调用统一创建流程。
    // <<< MAIN-DEFENSE
    account_contract::Status createInitialAdmin(const QString& username, const QString& password) override;
    // >>> MAIN-DEFENSE | 方法声明｜registerUser
    // 通过统一创建流程建立普通用户，不允许注册入口自己选择管理员角色。
    // <<< MAIN-DEFENSE
    account_contract::Status registerUser(const QString& username, const QString& password) override;
    // >>> MAIN-DEFENSE | 方法声明｜login
    // 按规范化用户名读密码哈希，验证明文输入；成功后建立当前会话，失败不能保留旧身份。
    // <<< MAIN-DEFENSE
    account_contract::Result<account_contract::UserIdentity> login(const QString& username, const QString& password) override;
    // >>> MAIN-DEFENSE | 方法声明｜currentUser
    // 读取当前会话身份；用于权限/个人历史校验，不是从界面显示文字判断用户。
    // <<< MAIN-DEFENSE
    std::optional<account_contract::UserIdentity> currentUser() const override;
    // >>> MAIN-DEFENSE | 方法声明｜logout
    // 清当前登录状态，使后续受保护操作不能沿用退出前身份。
    // <<< MAIN-DEFENSE
    void logout() override;
    // >>> MAIN-DEFENSE | 方法声明｜addMedia
    // 先业务层管理员校验，再新增媒体，保留保存与同步结果供Qt判断。
    // <<< MAIN-DEFENSE
    account_contract::MediaMutationResult addMedia(const MediaRecord& record) override;
    // >>> MAIN-DEFENSE | 方法声明｜removeMedia
    // 先业务层管理员校验，再删除媒体，并传递是否已经持久化，避免盲目重试。
    // <<< MAIN-DEFENSE
    account_contract::MediaMutationResult removeMedia(const QString& mediaId) override;
    // >>> MAIN-DEFENSE | 方法声明｜recordMediaView
    // 校验本人身份和媒体引用，更新个人浏览次数与最后浏览时间，不记录自动选中。
    // <<< MAIN-DEFENSE
    account_contract::Status recordMediaView(qint64 userId, const QString& mediaId) override;
    // >>> MAIN-DEFENSE | 方法声明｜loadMediaHistory
    // 读取当前用户的浏览历史，数据库错误与正常空历史分开返回。
    // <<< MAIN-DEFENSE
    account_contract::Result<QList<account_contract::MediaHistoryEntry>> loadMediaHistory(qint64 userId) override;
private:
    // >>> MAIN-DEFENSE | 方法声明｜createUser
    // 验证用户名密码与首次管理员条件，对密码做带盐哈希，使用绑定参数写入用户。
    // <<< MAIN-DEFENSE
    account_contract::Status createUser(const QString& username, const QString& password, bool admin);
    // >>> MAIN-DEFENSE | 方法声明｜checkUser
    // 核对当前会话与目标用户身份，阻止读取或写入其他用户个人历史。
    // <<< MAIN-DEFENSE
    account_contract::Status checkUser(qint64 userId) const;
    // >>> MAIN-DEFENSE | 方法声明｜checkAdmin
    // 在业务层检查单机管理员身份，不能只依赖Qt隐藏按钮。
    // <<< MAIN-DEFENSE
    account_contract::Status checkAdmin() const;
    // >>> MAIN-DEFENSE | 方法声明｜ensureLibrary
    // 确认媒体库依赖可用并完成需要的加载，不把账户成功等同于媒体资料已同步。
    // <<< MAIN-DEFENSE
    account_contract::Status ensureLibrary();
    MediaLibrary* activeLibrary() const; // 返回当前使用的媒体库，不转移所有权。
    QString connectionName_;
    QSqlDatabase db_;
    QString csvPath_;
    std::unique_ptr<MediaLibrary> library_;
    MediaLibrary* sharedLibrary_ = nullptr; // 非拥有指针，适用于与界面共享快照。
    std::optional<account_contract::UserIdentity> session_;
};
