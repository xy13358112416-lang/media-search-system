// >>> MAIN-DEFENSE | 主逻辑文件导读
// 单机账户和浏览历史：初始化SQLite、首次管理员、普通注册、密码哈希验证及会话、管理员媒体增删。在线仓库身份由另一套network_users和socket会话管理，不混用这两种权限模型。
// 阅读时先看函数入口说明，再按“处理阶段”往下；到每个return核对成功、空结果和错误是否区别。
// <<< MAIN-DEFENSE
#include "account_history_service.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>
#include <QVariant>
#include <sodium.h>
#include <utility>

using namespace account_contract;
namespace {
// >>> MAIN-DEFENSE | 函数入口｜failure
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：Status failure(ErrorCode code, const QString& message) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Status failure(ErrorCode code, const QString& message) { return {code, message}; }
// >>> MAIN-DEFENSE | 函数入口｜userKey
// 规范化用户名以建立稳定查询键；展示名和登录比较键可以不同。
// 签名提示：QString userKey(const QString& name) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
QString userKey(const QString& name) {
    return name.trimmed().normalized(QString::NormalizationForm_C).toCaseFolded().normalized(QString::NormalizationForm_C);
}
// >>> MAIN-DEFENSE | 函数入口｜execSql
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：bool execSql(QSqlDatabase& db, const QString& sql) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool execSql(QSqlDatabase& db, const QString& sql) { QSqlQuery q(db); return q.exec(sql); }
// >>> MAIN-DEFENSE | 函数入口｜rollback
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：void rollback(QSqlDatabase& db) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
void rollback(QSqlDatabase& db) { execSql(db, QStringLiteral("ROLLBACK")); }
struct ColumnSpec { QString name; QString type; bool required; int primaryKey; };
// >>> MAIN-DEFENSE | 函数入口｜schemaHas
// 通过SQLite元数据检查列的类型、主键和空值约束，不只检查表是否存在。
// 签名提示：bool schemaHas(QSqlDatabase& db, const QString& table, const QList<ColumnSpec>& expected) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool schemaHas(QSqlDatabase& db, const QString& table, const QList<ColumnSpec>& expected) {
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("PRAGMA table_info(") + table + QLatin1Char(')'))) return false;
    qsizetype index = 0;
    // >>> MAIN-DEFENSE | schemaHas｜处理阶段 01
    // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
    // <<< MAIN-DEFENSE
    while (q.next()) {
        if (index >= expected.size()) return false;
        const auto& column = expected.at(index++);
        if (q.value(1).toString() != column.name || q.value(2).toString().toUpper() != column.type ||
            q.value(3).toBool() != column.required || q.value(5).toInt() != column.primaryKey) return false;
    }
    // >>> MAIN-DEFENSE | schemaHas｜处理阶段 02
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return index == expected.size();
}
// >>> MAIN-DEFENSE | 函数入口｜hasUniqueUsernameKey
// 确认用户名查询键有唯一约束，阻止大小写/规范化后重复账号。
// 签名提示：bool hasUniqueUsernameKey(QSqlDatabase& db) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool hasUniqueUsernameKey(QSqlDatabase& db) {
    QSqlQuery indexes(db);
    if (!indexes.exec(QStringLiteral("PRAGMA index_list(users)"))) return false;
    while (indexes.next()) {
        // >>> MAIN-DEFENSE | hasUniqueUsernameKey｜处理阶段 01
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!indexes.value(2).toBool() || indexes.value(4).toBool()) continue;
        QString name = indexes.value(1).toString(); name.replace('\'', "''");
        QSqlQuery columns(db);
        if (!columns.exec(QStringLiteral("PRAGMA index_info('") + name + QStringLiteral("')"))) return false;
        QStringList names; while (columns.next()) names << columns.value(2).toString();
        // >>> MAIN-DEFENSE | hasUniqueUsernameKey｜处理阶段 02
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (names == QStringList{QStringLiteral("username_key")}) return true;
    }
    return false;
}
// >>> MAIN-DEFENSE | 函数入口｜hasHistoryForeignKey
// 确认历史记录外键指向用户表，避免无主历史记录。
// 签名提示：bool hasHistoryForeignKey(QSqlDatabase& db) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool hasHistoryForeignKey(QSqlDatabase& db) {
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("PRAGMA foreign_key_list(media_history)"))) return false;
    while (q.next())
        // >>> MAIN-DEFENSE | hasHistoryForeignKey｜处理阶段 01
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (q.value(2).toString() == "users" && q.value(3).toString() == "user_id" && q.value(4).toString() == "user_id") return true;
    return false;
}
// >>> MAIN-DEFENSE | 函数入口｜hasRequiredChecks
// 读取建表SQL并核对关键CHECK约束，避免版本号正确但字段约束缺失。
// 签名提示：bool hasRequiredChecks(QSqlDatabase& db, const QString& table, const QString& fragment) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool hasRequiredChecks(QSqlDatabase& db, const QString& table, const QString& fragment) {
    QSqlQuery q(db); q.prepare(QStringLiteral("SELECT sql FROM sqlite_master WHERE type='table' AND name=?")); q.addBindValue(table);
    if (!q.exec() || !q.next()) return false;
    QString sql = q.value(0).toString().toLower();
    sql.remove(QRegularExpression(QStringLiteral("\\s+")));
    // >>> MAIN-DEFENSE | hasRequiredChecks｜处理阶段 01
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return sql.contains(fragment);
}
}

// >>> MAIN-DEFENSE | 函数入口｜AccountHistoryServiceImpl::AccountHistoryServiceImpl
// 构造依赖与初始状态；参数通过初始化列表保存，本段不能当成登录或数据加载已经完成。
// 签名提示：AccountHistoryServiceImpl::AccountHistoryServiceImpl(QString csvPath)
//       : connectionName_(QUuid::createUuid().toString(QUuid::WithoutBraces)), csvPath_(std::move(csvPath))
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
AccountHistoryServiceImpl::AccountHistoryServiceImpl(QString csvPath)
    : connectionName_(QUuid::createUuid().toString(QUuid::WithoutBraces)), csvPath_(std::move(csvPath)) {}

// >>> MAIN-DEFENSE | 函数入口｜AccountHistoryServiceImpl::AccountHistoryServiceImpl
// 构造依赖与初始状态；参数通过初始化列表保存，本段不能当成登录或数据加载已经完成。
// 签名提示：AccountHistoryServiceImpl::AccountHistoryServiceImpl(MediaLibrary& library)
//       : connectionName_(QUuid::createUuid().toString(QUuid::WithoutBraces)), sharedLibrary_(&library)
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
AccountHistoryServiceImpl::AccountHistoryServiceImpl(MediaLibrary& library)
    : connectionName_(QUuid::createUuid().toString(QUuid::WithoutBraces)), sharedLibrary_(&library) {}

// >>> MAIN-DEFENSE | 函数入口｜AccountHistoryServiceImpl::~AccountHistoryServiceImpl
// 析构收尾：释放本对象持有的连接/请求等资源；注意Qt回调与数据库句柄的生命周期。
// 签名提示：AccountHistoryServiceImpl::~AccountHistoryServiceImpl() 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
AccountHistoryServiceImpl::~AccountHistoryServiceImpl() {
    session_.reset(); library_.reset();
    if (db_.isValid()) db_.close();
    db_ = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName_);
}

// >>> MAIN-DEFENSE | 函数入口｜AccountHistoryServiceImpl::defaultDatabasePath
// 在应用本地数据目录计算单机账户库路径，不把密码放到工程源码目录。
// 签名提示：QString AccountHistoryServiceImpl::defaultDatabasePath() 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
QString AccountHistoryServiceImpl::defaultDatabasePath() {
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return directory.isEmpty() ? QString() : QDir(directory).filePath(QStringLiteral("accounts.sqlite"));
}

// >>> MAIN-DEFENSE | 函数入口｜AccountHistoryServiceImpl::initializeDefaultDatabase
// 创建本地数据目录并调用数据库初始化；失败清理旧状态，避免沿用错误会话。
// 签名提示：Status AccountHistoryServiceImpl::initializeDefaultDatabase() 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Status AccountHistoryServiceImpl::initializeDefaultDatabase() {
    const QString path = defaultDatabasePath();
    if (path.isEmpty() || !QDir().mkpath(QFileInfo(path).absolutePath())) {
        initializeDatabase(QString()); // 失败时关闭旧数据库并清除会话。
        // >>> MAIN-DEFENSE | initializeDefaultDatabase｜处理阶段 01
        // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
        // <<< MAIN-DEFENSE
        return failure(ErrorCode::DatabaseError, QStringLiteral("无法创建应用数据目录"));
    }
    return initializeDatabase(path);
}

// >>> MAIN-DEFENSE | 函数入口｜AccountHistoryServiceImpl::initializeDatabase
// 清旧会话/连接，初始化密码组件，打开SQLite并核对或创建表结构，配置外键和等待超时。
// 签名提示：Status AccountHistoryServiceImpl::initializeDatabase(const QString& databasePath) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Status AccountHistoryServiceImpl::initializeDatabase(const QString& databasePath) {
    session_.reset(); library_.reset();
    if (db_.isValid()) { db_.close(); db_ = QSqlDatabase(); QSqlDatabase::removeDatabase(connectionName_); }
    if (databasePath.trimmed().isEmpty()) return failure(ErrorCode::InvalidArgument, QStringLiteral("数据库路径为空"));
    // >>> MAIN-DEFENSE | initializeDatabase｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (sodium_init() < 0) return failure(ErrorCode::PasswordHashError, QStringLiteral("密码组件初始化失败"));
    db_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
    db_.setDatabaseName(databasePath);
    if (!db_.open()) return failure(ErrorCode::DatabaseError, QStringLiteral("无法打开账户数据库"));
    // >>> MAIN-DEFENSE | initializeDatabase｜处理阶段 02
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!execSql(db_, QStringLiteral("PRAGMA foreign_keys=ON")) || !execSql(db_, QStringLiteral("PRAGMA busy_timeout=5000"))) {
        db_.close(); return failure(ErrorCode::DatabaseError, QStringLiteral("无法设置数据库连接"));
    }
    if (!execSql(db_, QStringLiteral("BEGIN IMMEDIATE"))) { db_.close(); return failure(ErrorCode::DatabaseError, QStringLiteral("无法启动数据库事务")); }
    QSqlQuery versionQuery(db_);
    // >>> MAIN-DEFENSE | initializeDatabase｜处理阶段 03
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!versionQuery.exec(QStringLiteral("PRAGMA user_version")) || !versionQuery.next()) { rollback(db_); db_.close(); return failure(ErrorCode::DatabaseError, QStringLiteral("无法读取数据库版本")); }
    const int version = versionQuery.value(0).toInt(); versionQuery.finish();
    if (version < 0 || version > 1) { rollback(db_); db_.close(); return failure(ErrorCode::DatabaseError, QStringLiteral("不支持的数据库版本")); }
    if (version == 0) {
        QSqlQuery existing(db_);
        // >>> MAIN-DEFENSE | initializeDatabase｜处理阶段 04
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!existing.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type='table' AND name IN ('users','media_history')"))) {
            rollback(db_); db_.close(); return failure(ErrorCode::DatabaseError, QStringLiteral("无法检查旧数据库"));
        }
        if (existing.next()) {
            existing.finish(); rollback(db_); db_.close();
            // >>> MAIN-DEFENSE | initializeDatabase｜处理阶段 05
            // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
            // <<< MAIN-DEFENSE
            return failure(ErrorCode::DatabaseError, QStringLiteral("检测到未版本化的账户或历史表，请先完成数据迁移"));
        }
        existing.finish();
    }
    const bool ok = execSql(db_, QStringLiteral("CREATE TABLE IF NOT EXISTS users (user_id INTEGER PRIMARY KEY AUTOINCREMENT, username TEXT NOT NULL, username_key TEXT NOT NULL UNIQUE, password_hash TEXT NOT NULL, role TEXT NOT NULL CHECK(role IN ('user','admin')), created_at INTEGER NOT NULL)")) &&
        execSql(db_, QStringLiteral("CREATE TABLE IF NOT EXISTS media_history (user_id INTEGER NOT NULL REFERENCES users(user_id), media_id TEXT NOT NULL, view_count INTEGER NOT NULL CHECK(view_count >= 0), last_viewed_at INTEGER NOT NULL, PRIMARY KEY(user_id, media_id))")) &&
        schemaHas(db_, QStringLiteral("users"), {{"user_id","INTEGER",false,1}, {"username","TEXT",true,0}, {"username_key","TEXT",true,0}, {"password_hash","TEXT",true,0}, {"role","TEXT",true,0}, {"created_at","INTEGER",true,0}}) &&
        schemaHas(db_, QStringLiteral("media_history"), {{"user_id","INTEGER",true,1}, {"media_id","TEXT",true,2}, {"view_count","INTEGER",true,0}, {"last_viewed_at","INTEGER",true,0}}) &&
        hasUniqueUsernameKey(db_) && hasHistoryForeignKey(db_) &&
        hasRequiredChecks(db_, QStringLiteral("users"), QStringLiteral("check(rolein('user','admin'))")) &&
        hasRequiredChecks(db_, QStringLiteral("media_history"), QStringLiteral("check(view_count>=0)")) &&
        execSql(db_, QStringLiteral("PRAGMA user_version=1"));
    // >>> MAIN-DEFENSE | initializeDatabase｜处理阶段 06
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!ok || !execSql(db_, QStringLiteral("COMMIT"))) {
        rollback(db_); db_.close(); return failure(ErrorCode::DatabaseError, QStringLiteral("数据库结构不兼容或初始化失败"));
    }
    return {};
}

// >>> MAIN-DEFENSE | 函数入口｜AccountHistoryServiceImpl::needsInitialAdmin
// 查询单机库是否还需要首次管理员；不是把任意注册用户提升权限。
// 签名提示：Result<bool> AccountHistoryServiceImpl::needsInitialAdmin() 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Result<bool> AccountHistoryServiceImpl::needsInitialAdmin() {
    if (!db_.isOpen()) return {failure(ErrorCode::NotInitialized, QStringLiteral("请先初始化数据库")), {}};
    QSqlQuery q(db_);
    if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM users")) || !q.next()) return {failure(ErrorCode::DatabaseError, QStringLiteral("查询用户失败")), {}};
    // >>> MAIN-DEFENSE | needsInitialAdmin｜处理阶段 01
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return {{}, q.value(0).toLongLong() == 0};
}

// >>> MAIN-DEFENSE | 函数入口｜AccountHistoryServiceImpl::createUser
// 验证用户名密码与首次管理员条件，对密码做带盐哈希，使用绑定参数写入用户。
// 签名提示：Status AccountHistoryServiceImpl::createUser(const QString& username, const QString& password, bool admin) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Status AccountHistoryServiceImpl::createUser(const QString& username, const QString& password, bool admin) {
    if (!db_.isOpen()) return failure(ErrorCode::NotInitialized, QStringLiteral("请先初始化数据库"));
    const QString display = username.trimmed().normalized(QString::NormalizationForm_C);
    const QString key = userKey(username);
    const QByteArray bytes = password.toUtf8();
    // >>> MAIN-DEFENSE | createUser｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (key.isEmpty() || bytes.isEmpty() || static_cast<unsigned long long>(bytes.size()) > crypto_pwhash_PASSWD_MAX)
        return failure(ErrorCode::InvalidArgument, QStringLiteral("用户名或口令无效"));
    char hash[crypto_pwhash_STRBYTES] = {};
    if (crypto_pwhash_str_alg(hash, bytes.constData(), static_cast<unsigned long long>(bytes.size()),
                              crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE,
                              crypto_pwhash_ALG_ARGON2ID13) != 0)
        // >>> MAIN-DEFENSE | createUser｜处理阶段 02
        // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
        // <<< MAIN-DEFENSE
        return failure(ErrorCode::PasswordHashError, QStringLiteral("无法生成口令哈希"));
    if (!execSql(db_, QStringLiteral("BEGIN IMMEDIATE"))) return failure(ErrorCode::DatabaseError, QStringLiteral("无法启动写事务"));
    QSqlQuery count(db_);
    if (!count.exec(QStringLiteral("SELECT COUNT(*) FROM users")) || !count.next()) { rollback(db_); return failure(ErrorCode::DatabaseError, QStringLiteral("查询用户失败")); }
    const bool empty = count.value(0).toLongLong() == 0; count.finish();
    // >>> MAIN-DEFENSE | createUser｜处理阶段 03
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (admin && !empty) { rollback(db_); return failure(ErrorCode::InitialAdminAlreadyExists, QStringLiteral("管理员已初始化")); }
    if (!admin && empty) { rollback(db_); return failure(ErrorCode::InitialAdminRequired, QStringLiteral("请先创建管理员")); }
    QSqlQuery exists(db_); exists.prepare(QStringLiteral("SELECT 1 FROM users WHERE username_key=?")); exists.addBindValue(key);
    if (!exists.exec()) { rollback(db_); return failure(ErrorCode::DatabaseError, QStringLiteral("查询用户失败")); }
    // >>> MAIN-DEFENSE | createUser｜处理阶段 04
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (exists.next()) { exists.finish(); rollback(db_); return failure(ErrorCode::UsernameTaken, QStringLiteral("用户名已存在")); }
    exists.finish();
    QSqlQuery insert(db_);
    insert.prepare(QStringLiteral("INSERT INTO users(username,username_key,password_hash,role,created_at) VALUES(?,?,?,?,?)"));
    insert.addBindValue(display); insert.addBindValue(key); insert.addBindValue(QString::fromLatin1(hash));
    insert.addBindValue(admin ? QStringLiteral("admin") : QStringLiteral("user")); insert.addBindValue(QDateTime::currentSecsSinceEpoch());
    // >>> MAIN-DEFENSE | createUser｜处理阶段 05
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!insert.exec()) { rollback(db_); return failure(ErrorCode::DatabaseError, QStringLiteral("创建账户失败")); }
    insert.finish();
    if (!execSql(db_, QStringLiteral("COMMIT"))) { rollback(db_); return failure(ErrorCode::DatabaseError, QStringLiteral("提交账户失败")); }
    return {};
}
// >>> MAIN-DEFENSE | 函数入口｜AccountHistoryServiceImpl::createInitialAdmin
// 只在允许首次建管理员的状态调用统一创建流程。
// 签名提示：Status AccountHistoryServiceImpl::createInitialAdmin(const QString& username, const QString& password) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Status AccountHistoryServiceImpl::createInitialAdmin(const QString& username, const QString& password) { return createUser(username, password, true); }
// >>> MAIN-DEFENSE | 函数入口｜AccountHistoryServiceImpl::registerUser
// 通过统一创建流程建立普通用户，不允许注册入口自己选择管理员角色。
// 签名提示：Status AccountHistoryServiceImpl::registerUser(const QString& username, const QString& password) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Status AccountHistoryServiceImpl::registerUser(const QString& username, const QString& password) { return createUser(username, password, false); }

// >>> MAIN-DEFENSE | 函数入口｜AccountHistoryServiceImpl::login
// 按规范化用户名读密码哈希，验证明文输入；成功后建立当前会话，失败不能保留旧身份。
// 签名提示：Result<UserIdentity> AccountHistoryServiceImpl::login(const QString& username, const QString& password) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Result<UserIdentity> AccountHistoryServiceImpl::login(const QString& username, const QString& password) {
    session_.reset();
    if (!db_.isOpen()) return {failure(ErrorCode::NotInitialized, QStringLiteral("请先初始化数据库")), {}};
    const QString key = userKey(username); const QByteArray bytes = password.toUtf8();
    // >>> MAIN-DEFENSE | login｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (key.isEmpty() || bytes.isEmpty()) return {failure(ErrorCode::InvalidArgument, QStringLiteral("用户名或口令为空")), {}};
    QSqlQuery q(db_); q.prepare(QStringLiteral("SELECT user_id,username,password_hash,role FROM users WHERE username_key=?")); q.addBindValue(key);
    if (!q.exec()) return {failure(ErrorCode::DatabaseError, QStringLiteral("账户查询失败")), {}};
    if (!q.next()) return {failure(ErrorCode::AuthenticationFailed, QStringLiteral("用户名或口令错误")), {}};
    const QByteArray hash = q.value(2).toString().toLatin1();
    // >>> MAIN-DEFENSE | login｜处理阶段 02
    // 验证密码：拿输入密码与存储的带盐哈希比较，不解密密码；失败不建立会话。
    // <<< MAIN-DEFENSE
    if (crypto_pwhash_str_verify(hash.constData(), bytes.constData(), static_cast<unsigned long long>(bytes.size())) != 0)
        return {failure(ErrorCode::AuthenticationFailed, QStringLiteral("用户名或口令错误")), {}};
    const QString role = q.value(3).toString();
    if (role != "admin" && role != "user") return {failure(ErrorCode::DatabaseError, QStringLiteral("账户角色无效")), {}};
    session_ = UserIdentity{q.value(0).toLongLong(), q.value(1).toString(), role == "admin" ? UserRole::Admin : UserRole::User};
    // >>> MAIN-DEFENSE | login｜处理阶段 03
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return {{}, session_};
}
// >>> MAIN-DEFENSE | 函数入口｜AccountHistoryServiceImpl::currentUser
// 读取当前会话身份；用于权限/个人历史校验，不是从界面显示文字判断用户。
// 签名提示：std::optional<UserIdentity> AccountHistoryServiceImpl::currentUser() const 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
std::optional<UserIdentity> AccountHistoryServiceImpl::currentUser() const { return session_; }
// >>> MAIN-DEFENSE | 函数入口｜AccountHistoryServiceImpl::logout
// 清当前登录状态，使后续受保护操作不能沿用退出前身份。
// 签名提示：void AccountHistoryServiceImpl::logout() 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
void AccountHistoryServiceImpl::logout() { session_.reset(); }

// >>> MAIN-DEFENSE | 函数入口｜AccountHistoryServiceImpl::checkUser
// 核对当前会话与目标用户身份，阻止读取或写入其他用户个人历史。
// 签名提示：Status AccountHistoryServiceImpl::checkUser(qint64 userId) const 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Status AccountHistoryServiceImpl::checkUser(qint64 userId) const {
    if (!db_.isOpen()) return failure(ErrorCode::NotInitialized, QStringLiteral("请先初始化数据库"));
    if (!session_) return failure(ErrorCode::NotAuthenticated, QStringLiteral("请先登录"));
    if (session_->userId != userId) return failure(ErrorCode::PermissionDenied, QStringLiteral("只能访问当前用户的历史"));
    // >>> MAIN-DEFENSE | checkUser｜处理阶段 01
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return {};
}
// >>> MAIN-DEFENSE | 函数入口｜AccountHistoryServiceImpl::checkAdmin
// 在业务层检查单机管理员身份，不能只依赖Qt隐藏按钮。
// 签名提示：Status AccountHistoryServiceImpl::checkAdmin() const 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Status AccountHistoryServiceImpl::checkAdmin() const {
    if (!db_.isOpen()) return failure(ErrorCode::NotInitialized, QStringLiteral("请先初始化数据库"));
    if (!session_) return failure(ErrorCode::NotAuthenticated, QStringLiteral("请先登录"));
    if (session_->role != UserRole::Admin) return failure(ErrorCode::PermissionDenied, QStringLiteral("仅管理员可修改媒体"));
    // >>> MAIN-DEFENSE | checkAdmin｜处理阶段 01
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return {};
}
// >>> MAIN-DEFENSE | 函数入口｜AccountHistoryServiceImpl::activeLibrary
// 选择注入的共享MediaLibrary或服务内部持有的库；注意与界面保持同一数据源。
// 签名提示：MediaLibrary* AccountHistoryServiceImpl::activeLibrary() const 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
MediaLibrary* AccountHistoryServiceImpl::activeLibrary() const {
    return sharedLibrary_ ? sharedLibrary_ : library_.get();
}

// >>> MAIN-DEFENSE | 函数入口｜AccountHistoryServiceImpl::ensureLibrary
// 确认媒体库依赖可用并完成需要的加载，不把账户成功等同于媒体资料已同步。
// 签名提示：Status AccountHistoryServiceImpl::ensureLibrary() 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Status AccountHistoryServiceImpl::ensureLibrary() {
    if (!sharedLibrary_ && csvPath_.isEmpty()) return failure(ErrorCode::StorageError, QStringLiteral("尚未配置媒体库路径"));
    if (!sharedLibrary_ && !library_) library_ = std::make_unique<MediaLibrary>(csvPath_, QString());
    // 主程序其他模块可能修改 CSV；每次业务操作之前重新加载并重建本服务索引。
    const auto result = activeLibrary()->load();
    // >>> MAIN-DEFENSE | ensureLibrary｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!result.success) return failure(ErrorCode::StorageError, result.error);
    return {};
}

// >>> MAIN-DEFENSE | 函数入口｜AccountHistoryServiceImpl::addMedia
// 先业务层管理员校验，再新增媒体，保留保存与同步结果供Qt判断。
// 签名提示：MediaMutationResult AccountHistoryServiceImpl::addMedia(const MediaRecord& record) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
MediaMutationResult AccountHistoryServiceImpl::addMedia(const MediaRecord& record) {
    auto status = checkAdmin(); if (!status.ok()) return {status, false, {}};
    const auto validation = CsvMediaRepository::validateNewRecord(record);
    if (!validation.success) return {failure(ErrorCode::InvalidArgument, validation.error), false, {}};
    status = ensureLibrary(); if (!status.ok()) return {status, false, {}};
    MediaRecord candidate = record;
    const auto result = activeLibrary()->addNew(candidate);
    // >>> MAIN-DEFENSE | addMedia｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!result.success) return {failure(result.persisted ? ErrorCode::IndexSyncError : ErrorCode::StorageError, result.error), result.persisted, result.persisted ? candidate.id : QString()};
    return {{}, true, candidate.id};
}
// >>> MAIN-DEFENSE | 函数入口｜AccountHistoryServiceImpl::removeMedia
// 先业务层管理员校验，再删除媒体，并传递是否已经持久化，避免盲目重试。
// 签名提示：MediaMutationResult AccountHistoryServiceImpl::removeMedia(const QString& mediaId) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
MediaMutationResult AccountHistoryServiceImpl::removeMedia(const QString& mediaId) {
    auto status = checkAdmin(); if (!status.ok()) return {status, false, {}};
    if (mediaId.trimmed().isEmpty()) return {failure(ErrorCode::InvalidArgument, QStringLiteral("媒体 ID 为空")), false, {}};
    status = ensureLibrary(); if (!status.ok()) return {status, false, {}};
    MediaRecord record;
    // >>> MAIN-DEFENSE | removeMedia｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!activeLibrary()->find(mediaId, record)) return {failure(ErrorCode::MediaNotFound, QStringLiteral("媒体不存在")), false, {}};
    const auto result = activeLibrary()->remove(mediaId);
    if (!result.success) return {failure(result.persisted ? ErrorCode::IndexSyncError : ErrorCode::StorageError, result.error), result.persisted, result.persisted ? mediaId : QString()};
    return {{}, true, mediaId};
}

// >>> MAIN-DEFENSE | 函数入口｜AccountHistoryServiceImpl::recordMediaView
// 校验本人身份和媒体引用，更新个人浏览次数与最后浏览时间，不记录自动选中。
// 签名提示：Status AccountHistoryServiceImpl::recordMediaView(qint64 userId, const QString& mediaId) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Status AccountHistoryServiceImpl::recordMediaView(qint64 userId, const QString& mediaId) {
    auto status = checkUser(userId); if (!status.ok()) return status;
    if (mediaId.trimmed().isEmpty()) return failure(ErrorCode::InvalidArgument, QStringLiteral("媒体 ID 为空"));
    status = ensureLibrary(); if (!status.ok()) return status;
    MediaRecord record;
    // >>> MAIN-DEFENSE | recordMediaView｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!activeLibrary()->find(mediaId, record)) return failure(ErrorCode::MediaNotFound, QStringLiteral("媒体不存在"));
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("INSERT INTO media_history(user_id,media_id,view_count,last_viewed_at) VALUES(?,?,1,?) ON CONFLICT(user_id,media_id) DO UPDATE SET view_count=view_count+1,last_viewed_at=excluded.last_viewed_at WHERE view_count<9223372036854775807"));
    q.addBindValue(userId); q.addBindValue(mediaId); q.addBindValue(QDateTime::currentSecsSinceEpoch());
    // >>> MAIN-DEFENSE | recordMediaView｜处理阶段 02
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!q.exec() || q.numRowsAffected() != 1) return failure(ErrorCode::DatabaseError, QStringLiteral("记录浏览历史失败"));
    return {};
}
// >>> MAIN-DEFENSE | 函数入口｜AccountHistoryServiceImpl::loadMediaHistory
// 读取当前用户的浏览历史，数据库错误与正常空历史分开返回。
// 签名提示：Result<QList<MediaHistoryEntry>> AccountHistoryServiceImpl::loadMediaHistory(qint64 userId) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Result<QList<MediaHistoryEntry>> AccountHistoryServiceImpl::loadMediaHistory(qint64 userId) {
    auto status = checkUser(userId); if (!status.ok()) return {status, {}};
    QSqlQuery q(db_); q.prepare(QStringLiteral("SELECT media_id,view_count,last_viewed_at FROM media_history WHERE user_id=? ORDER BY media_id")); q.addBindValue(userId);
    if (!q.exec()) return {failure(ErrorCode::DatabaseError, QStringLiteral("读取浏览历史失败")), {}};
    QList<MediaHistoryEntry> result;
    // >>> MAIN-DEFENSE | loadMediaHistory｜处理阶段 01
    // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
    // <<< MAIN-DEFENSE
    while (q.next()) result.append({q.value(0).toString(), q.value(1).toLongLong(), q.value(2).toLongLong()});
    return {{}, result};
}
