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
Status failure(ErrorCode code, const QString& message) { return {code, message}; }
QString userKey(const QString& name) {
    return name.trimmed().normalized(QString::NormalizationForm_C).toCaseFolded().normalized(QString::NormalizationForm_C);
}
bool execSql(QSqlDatabase& db, const QString& sql) { QSqlQuery q(db); return q.exec(sql); }
void rollback(QSqlDatabase& db) { execSql(db, QStringLiteral("ROLLBACK")); }
struct ColumnSpec { QString name; QString type; bool required; int primaryKey; };
bool schemaHas(QSqlDatabase& db, const QString& table, const QList<ColumnSpec>& expected) {
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("PRAGMA table_info(") + table + QLatin1Char(')'))) return false;
    qsizetype index = 0;
    while (q.next()) {
        if (index >= expected.size()) return false;
        const auto& column = expected.at(index++);
        if (q.value(1).toString() != column.name || q.value(2).toString().toUpper() != column.type ||
            q.value(3).toBool() != column.required || q.value(5).toInt() != column.primaryKey) return false;
    }
    return index == expected.size();
}
bool hasUniqueUsernameKey(QSqlDatabase& db) {
    QSqlQuery indexes(db);
    if (!indexes.exec(QStringLiteral("PRAGMA index_list(users)"))) return false;
    while (indexes.next()) {
        if (!indexes.value(2).toBool() || indexes.value(4).toBool()) continue;
        QString name = indexes.value(1).toString(); name.replace('\'', "''");
        QSqlQuery columns(db);
        if (!columns.exec(QStringLiteral("PRAGMA index_info('") + name + QStringLiteral("')"))) return false;
        QStringList names; while (columns.next()) names << columns.value(2).toString();
        if (names == QStringList{QStringLiteral("username_key")}) return true;
    }
    return false;
}
bool hasHistoryForeignKey(QSqlDatabase& db) {
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("PRAGMA foreign_key_list(media_history)"))) return false;
    while (q.next())
        if (q.value(2).toString() == "users" && q.value(3).toString() == "user_id" && q.value(4).toString() == "user_id") return true;
    return false;
}
bool hasRequiredChecks(QSqlDatabase& db, const QString& table, const QString& fragment) {
    QSqlQuery q(db); q.prepare(QStringLiteral("SELECT sql FROM sqlite_master WHERE type='table' AND name=?")); q.addBindValue(table);
    if (!q.exec() || !q.next()) return false;
    QString sql = q.value(0).toString().toLower();
    sql.remove(QRegularExpression(QStringLiteral("\\s+")));
    return sql.contains(fragment);
}
}

AccountHistoryServiceImpl::AccountHistoryServiceImpl(QString csvPath)
    : connectionName_(QUuid::createUuid().toString(QUuid::WithoutBraces)), csvPath_(std::move(csvPath)) {}

AccountHistoryServiceImpl::~AccountHistoryServiceImpl() {
    session_.reset(); library_.reset();
    if (db_.isValid()) db_.close();
    db_ = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName_);
}

QString AccountHistoryServiceImpl::defaultDatabasePath() {
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return directory.isEmpty() ? QString() : QDir(directory).filePath(QStringLiteral("accounts.sqlite"));
}

Status AccountHistoryServiceImpl::initializeDefaultDatabase() {
    const QString path = defaultDatabasePath();
    if (path.isEmpty() || !QDir().mkpath(QFileInfo(path).absolutePath())) {
        initializeDatabase(QString()); // 失败时关闭旧数据库并清除会话。
        return failure(ErrorCode::DatabaseError, QStringLiteral("无法创建应用数据目录"));
    }
    return initializeDatabase(path);
}

Status AccountHistoryServiceImpl::initializeDatabase(const QString& databasePath) {
    session_.reset(); library_.reset();
    if (db_.isValid()) { db_.close(); db_ = QSqlDatabase(); QSqlDatabase::removeDatabase(connectionName_); }
    if (databasePath.trimmed().isEmpty()) return failure(ErrorCode::InvalidArgument, QStringLiteral("数据库路径为空"));
    if (sodium_init() < 0) return failure(ErrorCode::PasswordHashError, QStringLiteral("密码组件初始化失败"));
    db_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
    db_.setDatabaseName(databasePath);
    if (!db_.open()) return failure(ErrorCode::DatabaseError, QStringLiteral("无法打开账户数据库"));
    if (!execSql(db_, QStringLiteral("PRAGMA foreign_keys=ON")) || !execSql(db_, QStringLiteral("PRAGMA busy_timeout=5000"))) {
        db_.close(); return failure(ErrorCode::DatabaseError, QStringLiteral("无法设置数据库连接"));
    }
    if (!execSql(db_, QStringLiteral("BEGIN IMMEDIATE"))) { db_.close(); return failure(ErrorCode::DatabaseError, QStringLiteral("无法启动数据库事务")); }
    QSqlQuery versionQuery(db_);
    if (!versionQuery.exec(QStringLiteral("PRAGMA user_version")) || !versionQuery.next()) { rollback(db_); db_.close(); return failure(ErrorCode::DatabaseError, QStringLiteral("无法读取数据库版本")); }
    const int version = versionQuery.value(0).toInt(); versionQuery.finish();
    if (version < 0 || version > 1) { rollback(db_); db_.close(); return failure(ErrorCode::DatabaseError, QStringLiteral("不支持的数据库版本")); }
    if (version == 0) {
        QSqlQuery existing(db_);
        if (!existing.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type='table' AND name IN ('users','media_history')"))) {
            rollback(db_); db_.close(); return failure(ErrorCode::DatabaseError, QStringLiteral("无法检查旧数据库"));
        }
        if (existing.next()) {
            existing.finish(); rollback(db_); db_.close();
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
    if (!ok || !execSql(db_, QStringLiteral("COMMIT"))) {
        rollback(db_); db_.close(); return failure(ErrorCode::DatabaseError, QStringLiteral("数据库结构不兼容或初始化失败"));
    }
    return {};
}

Result<bool> AccountHistoryServiceImpl::needsInitialAdmin() {
    if (!db_.isOpen()) return {failure(ErrorCode::NotInitialized, QStringLiteral("请先初始化数据库")), {}};
    QSqlQuery q(db_);
    if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM users")) || !q.next()) return {failure(ErrorCode::DatabaseError, QStringLiteral("查询用户失败")), {}};
    return {{}, q.value(0).toLongLong() == 0};
}

Status AccountHistoryServiceImpl::createUser(const QString& username, const QString& password, bool admin) {
    if (!db_.isOpen()) return failure(ErrorCode::NotInitialized, QStringLiteral("请先初始化数据库"));
    const QString display = username.trimmed().normalized(QString::NormalizationForm_C);
    const QString key = userKey(username);
    const QByteArray bytes = password.toUtf8();
    if (key.isEmpty() || bytes.isEmpty() || static_cast<unsigned long long>(bytes.size()) > crypto_pwhash_PASSWD_MAX)
        return failure(ErrorCode::InvalidArgument, QStringLiteral("用户名或口令无效"));
    char hash[crypto_pwhash_STRBYTES] = {};
    if (crypto_pwhash_str_alg(hash, bytes.constData(), static_cast<unsigned long long>(bytes.size()),
                              crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE,
                              crypto_pwhash_ALG_ARGON2ID13) != 0)
        return failure(ErrorCode::PasswordHashError, QStringLiteral("无法生成口令哈希"));
    if (!execSql(db_, QStringLiteral("BEGIN IMMEDIATE"))) return failure(ErrorCode::DatabaseError, QStringLiteral("无法启动写事务"));
    QSqlQuery count(db_);
    if (!count.exec(QStringLiteral("SELECT COUNT(*) FROM users")) || !count.next()) { rollback(db_); return failure(ErrorCode::DatabaseError, QStringLiteral("查询用户失败")); }
    const bool empty = count.value(0).toLongLong() == 0; count.finish();
    if (admin && !empty) { rollback(db_); return failure(ErrorCode::InitialAdminAlreadyExists, QStringLiteral("管理员已初始化")); }
    if (!admin && empty) { rollback(db_); return failure(ErrorCode::InitialAdminRequired, QStringLiteral("请先创建管理员")); }
    QSqlQuery exists(db_); exists.prepare(QStringLiteral("SELECT 1 FROM users WHERE username_key=?")); exists.addBindValue(key);
    if (!exists.exec()) { rollback(db_); return failure(ErrorCode::DatabaseError, QStringLiteral("查询用户失败")); }
    if (exists.next()) { exists.finish(); rollback(db_); return failure(ErrorCode::UsernameTaken, QStringLiteral("用户名已存在")); }
    exists.finish();
    QSqlQuery insert(db_);
    insert.prepare(QStringLiteral("INSERT INTO users(username,username_key,password_hash,role,created_at) VALUES(?,?,?,?,?)"));
    insert.addBindValue(display); insert.addBindValue(key); insert.addBindValue(QString::fromLatin1(hash));
    insert.addBindValue(admin ? QStringLiteral("admin") : QStringLiteral("user")); insert.addBindValue(QDateTime::currentSecsSinceEpoch());
    if (!insert.exec()) { rollback(db_); return failure(ErrorCode::DatabaseError, QStringLiteral("创建账户失败")); }
    insert.finish();
    if (!execSql(db_, QStringLiteral("COMMIT"))) { rollback(db_); return failure(ErrorCode::DatabaseError, QStringLiteral("提交账户失败")); }
    return {};
}
Status AccountHistoryServiceImpl::createInitialAdmin(const QString& username, const QString& password) { return createUser(username, password, true); }
Status AccountHistoryServiceImpl::registerUser(const QString& username, const QString& password) { return createUser(username, password, false); }

Result<UserIdentity> AccountHistoryServiceImpl::login(const QString& username, const QString& password) {
    session_.reset();
    if (!db_.isOpen()) return {failure(ErrorCode::NotInitialized, QStringLiteral("请先初始化数据库")), {}};
    const QString key = userKey(username); const QByteArray bytes = password.toUtf8();
    if (key.isEmpty() || bytes.isEmpty()) return {failure(ErrorCode::InvalidArgument, QStringLiteral("用户名或口令为空")), {}};
    QSqlQuery q(db_); q.prepare(QStringLiteral("SELECT user_id,username,password_hash,role FROM users WHERE username_key=?")); q.addBindValue(key);
    if (!q.exec()) return {failure(ErrorCode::DatabaseError, QStringLiteral("账户查询失败")), {}};
    if (!q.next()) return {failure(ErrorCode::AuthenticationFailed, QStringLiteral("用户名或口令错误")), {}};
    const QByteArray hash = q.value(2).toString().toLatin1();
    if (crypto_pwhash_str_verify(hash.constData(), bytes.constData(), static_cast<unsigned long long>(bytes.size())) != 0)
        return {failure(ErrorCode::AuthenticationFailed, QStringLiteral("用户名或口令错误")), {}};
    const QString role = q.value(3).toString();
    if (role != "admin" && role != "user") return {failure(ErrorCode::DatabaseError, QStringLiteral("账户角色无效")), {}};
    session_ = UserIdentity{q.value(0).toLongLong(), q.value(1).toString(), role == "admin" ? UserRole::Admin : UserRole::User};
    return {{}, session_};
}
std::optional<UserIdentity> AccountHistoryServiceImpl::currentUser() const { return session_; }
void AccountHistoryServiceImpl::logout() { session_.reset(); }

Status AccountHistoryServiceImpl::checkUser(qint64 userId) const {
    if (!db_.isOpen()) return failure(ErrorCode::NotInitialized, QStringLiteral("请先初始化数据库"));
    if (!session_) return failure(ErrorCode::NotAuthenticated, QStringLiteral("请先登录"));
    if (session_->userId != userId) return failure(ErrorCode::PermissionDenied, QStringLiteral("只能访问当前用户的历史"));
    return {};
}
Status AccountHistoryServiceImpl::checkAdmin() const {
    if (!db_.isOpen()) return failure(ErrorCode::NotInitialized, QStringLiteral("请先初始化数据库"));
    if (!session_) return failure(ErrorCode::NotAuthenticated, QStringLiteral("请先登录"));
    if (session_->role != UserRole::Admin) return failure(ErrorCode::PermissionDenied, QStringLiteral("仅管理员可修改媒体"));
    return {};
}
Status AccountHistoryServiceImpl::ensureLibrary() {
    if (csvPath_.isEmpty()) return failure(ErrorCode::StorageError, QStringLiteral("尚未配置媒体库路径"));
    if (!library_) library_ = std::make_unique<MediaLibrary>(csvPath_, QString());
    // 主程序其他模块可能修改 CSV；每次业务操作之前重新加载并重建本服务索引。
    const auto result = library_->load();
    if (!result.success) return failure(ErrorCode::StorageError, result.error);
    return {};
}

MediaMutationResult AccountHistoryServiceImpl::addMedia(const MediaRecord& record) {
    auto status = checkAdmin(); if (!status.ok()) return {status, false, {}};
    const auto validation = CsvMediaRepository::validateNewRecord(record);
    if (!validation.success) return {failure(ErrorCode::InvalidArgument, validation.error), false, {}};
    status = ensureLibrary(); if (!status.ok()) return {status, false, {}};
    MediaRecord candidate = record;
    const auto result = library_->addNew(candidate);
    if (!result.success) return {failure(result.persisted ? ErrorCode::IndexSyncError : ErrorCode::StorageError, result.error), result.persisted, result.persisted ? candidate.id : QString()};
    return {{}, true, candidate.id};
}
MediaMutationResult AccountHistoryServiceImpl::removeMedia(const QString& mediaId) {
    auto status = checkAdmin(); if (!status.ok()) return {status, false, {}};
    if (mediaId.trimmed().isEmpty()) return {failure(ErrorCode::InvalidArgument, QStringLiteral("媒体 ID 为空")), false, {}};
    status = ensureLibrary(); if (!status.ok()) return {status, false, {}};
    MediaRecord record;
    if (!library_->find(mediaId, record)) return {failure(ErrorCode::MediaNotFound, QStringLiteral("媒体不存在")), false, {}};
    const auto result = library_->remove(mediaId);
    if (!result.success) return {failure(result.persisted ? ErrorCode::IndexSyncError : ErrorCode::StorageError, result.error), result.persisted, result.persisted ? mediaId : QString()};
    return {{}, true, mediaId};
}

Status AccountHistoryServiceImpl::recordMediaView(qint64 userId, const QString& mediaId) {
    auto status = checkUser(userId); if (!status.ok()) return status;
    if (mediaId.trimmed().isEmpty()) return failure(ErrorCode::InvalidArgument, QStringLiteral("媒体 ID 为空"));
    status = ensureLibrary(); if (!status.ok()) return status;
    MediaRecord record;
    if (!library_->find(mediaId, record)) return failure(ErrorCode::MediaNotFound, QStringLiteral("媒体不存在"));
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("INSERT INTO media_history(user_id,media_id,view_count,last_viewed_at) VALUES(?,?,1,?) ON CONFLICT(user_id,media_id) DO UPDATE SET view_count=view_count+1,last_viewed_at=excluded.last_viewed_at WHERE view_count<9223372036854775807"));
    q.addBindValue(userId); q.addBindValue(mediaId); q.addBindValue(QDateTime::currentSecsSinceEpoch());
    if (!q.exec() || q.numRowsAffected() != 1) return failure(ErrorCode::DatabaseError, QStringLiteral("记录浏览历史失败"));
    return {};
}
Result<QList<MediaHistoryEntry>> AccountHistoryServiceImpl::loadMediaHistory(qint64 userId) {
    auto status = checkUser(userId); if (!status.ok()) return {status, {}};
    QSqlQuery q(db_); q.prepare(QStringLiteral("SELECT media_id,view_count,last_viewed_at FROM media_history WHERE user_id=? ORDER BY media_id")); q.addBindValue(userId);
    if (!q.exec()) return {failure(ErrorCode::DatabaseError, QStringLiteral("读取浏览历史失败")), {}};
    QList<MediaHistoryEntry> result;
    while (q.next()) result.append({q.value(0).toString(), q.value(1).toLongLong(), q.value(2).toLongLong()});
    return {{}, result};
}
