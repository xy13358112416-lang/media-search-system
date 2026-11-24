// >>> MAIN-DEFENSE | 主逻辑文件导读
// 在线业务总路由：连接与会话、账号验证、仓库资源权限、行为持久化及足迹/规则推荐快照。所有用户身份以当前连接session为准。
// 阅读时先看函数入口说明，再按“处理阶段”往下；到每个return核对成功、空结果和错误是否区别。
// <<< MAIN-DEFENSE
#include "warehouse_server.h"
#include "password_policy.h"
#include "media_json.h"
#include "behavior_contract.h"
#include "care_footprint.h"
#include "person_schema.h"
#include "person_store_sqlite.h"
#include "care_trending.h"
#include "care_temporal.h"
#include "person_snapshot_json.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QTcpSocket>
#include <QTimeZone>
#include <QUuid>
#include <memory>
#include <sodium.h>

using namespace warehouse_contract;

namespace {

// >>> MAIN-DEFENSE | 函数入口｜userKey
// 规范化用户名以建立稳定查询键；展示名和登录比较键可以不同。
// 签名提示：QString userKey(const QString& name)

// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
QString userKey(const QString& name)
{
    return name.trimmed().normalized(QString::NormalizationForm_C).toCaseFolded()
        .normalized(QString::NormalizationForm_C);
}

// >>> MAIN-DEFENSE | 函数入口｜readUtc
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：bool readUtc(const QJsonValue& value, qint64& result)

// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool readUtc(const QJsonValue& value, qint64& result)
{
    if (!value.isString()) return false;
    static const QRegularExpression format(QStringLiteral("\\A(?:0|[1-9][0-9]*)\\z"));
    const auto text = value.toString();
    bool ok = false; result = text.toLongLong(&ok);
    // >>> MAIN-DEFENSE | readUtc｜处理阶段 01
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return ok && format.match(text).hasMatch();
}
// >>> MAIN-DEFENSE | 函数入口｜readRange
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：bool readRange(const QJsonValue& value, care_contract::TimeRange& range)

// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool readRange(const QJsonValue& value, care_contract::TimeRange& range)
{
    if (!value.isObject()) return false;
    const auto o = value.toObject();
    // >>> MAIN-DEFENSE | readRange｜处理阶段 01
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return readUtc(o.value("begin_utc"), range.beginUtc) && readUtc(o.value("end_utc"), range.endUtc) &&
           care_contract::validate(range).ok();
}
// >>> MAIN-DEFENSE | 函数入口｜rangeJson
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：QJsonObject rangeJson(const care_contract::TimeRange& range)

// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
QJsonObject rangeJson(const care_contract::TimeRange& range)
{
    return {{"begin_utc", QString::number(range.beginUtc)}, {"end_utc", QString::number(range.endUtc)}};
}
// >>> MAIN-DEFENSE | 函数入口｜readPreference
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：bool readPreference(const QJsonValue& value, recommendation_contract::PreferenceSelection& preference)

// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool readPreference(const QJsonValue& value, recommendation_contract::PreferenceSelection& preference)
{
    if (!value.isObject()) return false;
    const auto object = value.toObject();
    // >>> MAIN-DEFENSE | readPreference｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!object.value("mode").isString() || !object.value("value").isString()) return false;
    const auto mode = object.value("mode").toString();
    if (mode == "auto") preference.mode = recommendation_contract::PreferenceMode::Auto;
    else if (mode == "any") preference.mode = recommendation_contract::PreferenceMode::Any;
    else if (mode == "explicit") preference.mode = recommendation_contract::PreferenceMode::Explicit;
    else return false;
    preference.value = object.value("value").toString();
    // >>> MAIN-DEFENSE | readPreference｜处理阶段 02
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return true;
}
// >>> MAIN-DEFENSE | 函数入口｜workJson
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：QJsonObject workJson(const care_contract::ResourceSummary& work)

// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
QJsonObject workJson(const care_contract::ResourceSummary& work)
{
    QJsonArray people;
    for (const auto& person : work.people) {
        QJsonArray roles;
        // >>> MAIN-DEFENSE | workJson｜处理阶段 01
        // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
        // <<< MAIN-DEFENSE
        for (const auto role : person.roles)
            roles.append(role == care_contract::PersonRole::Actor ? "actor" : role == care_contract::PersonRole::Director ? "director" : "musician");
        people.append(QJsonObject{{"person_id", person.personId}, {"name", person.name}, {"roles", roles}, {"followed", person.followed}});
    }
    // >>> MAIN-DEFENSE | workJson｜处理阶段 02
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return {{"resource", QJsonObject{{"work_id", work.resource.workId}, {"source", QJsonObject{
        {"warehouse_id", QString::number(work.resource.source.warehouseId)}, {"item_id", QString::number(work.resource.source.itemId)}}}}},
        {"media", media_json::toJson(work.media)}, {"people", people}};
}
// >>> MAIN-DEFENSE | 函数入口｜periodJson
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：QJsonObject periodJson(const care_contract::PeriodFootprint& period)

// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
QJsonObject periodJson(const care_contract::PeriodFootprint& period)
{
    QJsonArray categories, people, days;
    for (const auto& item : period.categories)
        categories.append(QJsonObject{{"key", item.key}, {"label", item.label}, {"count", QString::number(item.count)}});
    // >>> MAIN-DEFENSE | periodJson｜处理阶段 01
    // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
    // <<< MAIN-DEFENSE
    for (const auto& item : period.people) {
        const QString role = item.role == care_contract::PersonRole::Actor ? "actor" :
                             item.role == care_contract::PersonRole::Director ? "director" : "musician";
        people.append(QJsonObject{{"person_id", item.personId}, {"name", item.name}, {"role", role}, {"count", QString::number(item.count)}});
    }
    // >>> MAIN-DEFENSE | periodJson｜处理阶段 02
    // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
    // <<< MAIN-DEFENSE
    for (const auto& item : period.days)
        days.append(QJsonObject{{"local_date", item.localDate}, {"views", QString::number(item.views)}, {"searches", QString::number(item.searches)}});
    QJsonObject result{{"range", rangeJson(period.range)}, {"views", QString::number(period.views)},
        {"searches", QString::number(period.searches)}, {"unique_works", QString::number(period.uniqueWorks)},
        {"active_days", QString::number(period.activeDays)}, {"categories", categories}, {"people", people}, {"days", days}};
    // >>> MAIN-DEFENSE | periodJson｜处理阶段 03
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (period.coverageStartUtc) result.insert("coverage_start_utc", QString::number(*period.coverageStartUtc));
    return result;
}

// >>> MAIN-DEFENSE | 函数入口｜readId
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：qint64 readId(const QJsonValue& value)

// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
qint64 readId(const QJsonValue& value)
{
    const QString text = value.toString();
    static const QRegularExpression format(QStringLiteral("\\A[1-9][0-9]*\\z"));
    // >>> MAIN-DEFENSE | readId｜处理阶段 01
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return format.match(text).hasMatch() ? text.toLongLong() : 0;
}
// >>> MAIN-DEFENSE | 函数入口｜readResource
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：bool readResource(const QJsonValue& value, care_contract::ResourceRef& result)

// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool readResource(const QJsonValue& value, care_contract::ResourceRef& result)
{
    if (!value.isObject()) return false;
    const auto object = value.toObject(); const auto source = object.value("source");
    // >>> MAIN-DEFENSE | readResource｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!object.value("work_id").isString() || !source.isObject()) return false;
    result = {object.value("work_id").toString(), {readId(source.toObject().value("warehouse_id")), readId(source.toObject().value("item_id"))}};
    return care_contract::validate(result).ok();
}

// >>> MAIN-DEFENSE | 函数入口｜summaryJson
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：QJsonObject summaryJson(const WarehouseSummary& warehouse)

// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
QJsonObject summaryJson(const WarehouseSummary& warehouse)
{
    return {{"warehouse_id", QString::number(warehouse.warehouseId)}, {"name", warehouse.name},
            {"owner_id", QString::number(warehouse.ownerId)},
            {"visibility", warehouse.visibility == Visibility::Public ? "public" : "private"}};
}

// >>> MAIN-DEFENSE | 函数入口｜itemJson
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：QJsonObject itemJson(const WarehouseItem& item)

// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
QJsonObject itemJson(const WarehouseItem& item)
{
    return {{"item_id", QString::number(item.itemId)}, {"media", media_json::toJson(item.media)}};
}

// >>> MAIN-DEFENSE | 函数入口｜readSummary
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：WarehouseSummary readSummary(const QSqlQuery& query)

// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
WarehouseSummary readSummary(const QSqlQuery& query)
{
    return {query.value(0).toLongLong(), query.value(1).toString(), query.value(2).toLongLong(),
            query.value(3).toBool() ? Visibility::Public : Visibility::Private};
}

struct SnapshotBudget {
    int items = 0;
    qint64 bytes = 0;
};

std::optional<QList<WarehouseItem>> listItems(const QSqlDatabase& db, qint64 warehouseId,
                                              SnapshotBudget* budget = nullptr)
{
    QSqlQuery query(db);
    query.prepare("SELECT id,media FROM warehouse_items WHERE warehouse_id=? ORDER BY id");
    query.addBindValue(warehouseId);
    if (!query.exec())
        return std::nullopt;
    QList<WarehouseItem> items;
    while (query.next()) {
        const auto snapshot = query.value(1).toByteArray();
        if (budget) {
            ++budget->items;
            budget->bytes += snapshot.size();
            if (budget->items > 1000 || budget->bytes > warehouse_protocol::MaxFrameSize)
                return std::nullopt;
        }
        MediaRecord media;
        QString error;
        if (!media_json::fromJson(QJsonDocument::fromJson(snapshot).object(), media, error))
            return std::nullopt;
        items.append({query.value(0).toLongLong(), media});
    }
    return items;
}

} // namespace

// >>> MAIN-DEFENSE | 函数入口｜WarehouseServer::WarehouseServer
// 构造依赖与初始状态；参数通过初始化列表保存，本段不能当成登录或数据加载已经完成。
// 签名提示：WarehouseServer::WarehouseServer(llm_recommendation::Config config)
//       : personLookupNetwork_(new QNetworkAccessManager(this)), recommender_(config),
//         trendSummarizer_(config), sceneRecommender_(std::move(confi
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
WarehouseServer::WarehouseServer(llm_recommendation::Config config)
    : personLookupNetwork_(new QNetworkAccessManager(this)), recommender_(config),
      trendSummarizer_(config), sceneRecommender_(std::move(config)), dbName_(QUuid::createUuid().toString())
{
    // >>> MAIN-DEFENSE | WarehouseServer｜处理阶段 01
    // 注册异步信号回调：这里只登记后续要执行的处理，网络完成时才运行lambda，不是在此同步等结果。
    // <<< MAIN-DEFENSE
    connect(this, &QTcpServer::newConnection, this, [this] {
        while (auto* socket = nextPendingConnection()) {
            auto session = std::make_shared<Session>();
            sessions_.insert(socket, session);
            socket->setReadBufferSize(64 * 1024);
            // >>> MAIN-DEFENSE | WarehouseServer｜处理阶段 02
            // 注册异步信号回调：这里只登记后续要执行的处理，网络完成时才运行lambda，不是在此同步等结果。
            // <<< MAIN-DEFENSE
            connect(socket, &QTcpSocket::readyRead, this, [this, socket, session] {
                readRequests(socket, session);
            });
            connect(socket, &QTcpSocket::disconnected, this, [this, session, socket] {
                cancelPending(*session, socket, false);
                session->token.clear();
                // >>> MAIN-DEFENSE | WarehouseServer｜处理阶段 03
                // 使用当前连接的会话身份检查或构造业务上下文；客户端传来的编号不能替代登录身份。
                // <<< MAIN-DEFENSE
                session->userId = 0;
                sessions_.remove(socket);
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
}

// >>> MAIN-DEFENSE | 函数入口｜WarehouseServer::~WarehouseServer
// 析构收尾：释放本对象持有的连接/请求等资源；注意Qt回调与数据库句柄的生命周期。
// 签名提示：WarehouseServer::~WarehouseServer()

// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
WarehouseServer::~WarehouseServer()
{
    close();
    // Finish accepted connections while both async clients and the database still live.
    // disconnected cancels their pending jobs before callbacks can outlive this server.
    // >>> MAIN-DEFENSE | ~WarehouseServer｜处理阶段 01
    // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
    // <<< MAIN-DEFENSE
    for (auto* socket : findChildren<QTcpSocket*>()) socket->abort();
    delete personLookupNetwork_;
    personContextLoader_ = {};
    personStore_.reset();
    db_ = {};
    QSqlDatabase::removeDatabase(dbName_);
}

// >>> MAIN-DEFENSE | 函数入口｜WarehouseServer::openDatabase
// 打开在线仓库SQLite并初始化/核对schema，随后人物迁移复用此连接。
// 签名提示：bool WarehouseServer::openDatabase(const QString& path, QString& error)

// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool WarehouseServer::openDatabase(const QString& path, QString& error)
{
    error.clear();
    if (db_.isValid() || path.trimmed().isEmpty() || sodium_init() < 0) {
        error = QStringLiteral("无法初始化仓库数据库。");
        // >>> MAIN-DEFENSE | openDatabase｜处理阶段 01
        // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
        // <<< MAIN-DEFENSE
        return false;
    }
    db_ = QSqlDatabase::addDatabase("QSQLITE", dbName_);
    db_.setDatabaseName(path);
    QSqlQuery query(db_);
    // >>> MAIN-DEFENSE | openDatabase｜处理阶段 02
    // 开始数据库事务；下方相关写入必须一起成功，失败路径执行回滚。
    // <<< MAIN-DEFENSE
    if (!db_.open() || !query.exec("PRAGMA foreign_keys=ON") || !db_.transaction()) {
        error = QStringLiteral("无法打开仓库 SQLite 数据库。");
        return false;
    }
    // >>> MAIN-DEFENSE | openDatabase｜处理阶段 03
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!query.exec("CREATE TABLE IF NOT EXISTS network_users "
                    "(id INTEGER PRIMARY KEY, username TEXT NOT NULL, username_key TEXT NOT NULL UNIQUE, "
                    "password_hash TEXT NOT NULL)") ||
        !query.exec("CREATE TABLE IF NOT EXISTS warehouses "
                    "(id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, "
                    "owner_id INTEGER NOT NULL REFERENCES network_users(id), "
                    "is_public INTEGER NOT NULL CHECK(is_public IN (0,1)))") ||
        !query.exec("CREATE TABLE IF NOT EXISTS warehouse_items "
                    "(id INTEGER PRIMARY KEY AUTOINCREMENT, "
                    "warehouse_id INTEGER NOT NULL REFERENCES warehouses(id), media BLOB NOT NULL)") ||
        !query.exec("CREATE INDEX IF NOT EXISTS warehouse_items_warehouse ON warehouse_items(warehouse_id,id)") ||
        !query.exec("CREATE TABLE IF NOT EXISTS work_sources "
                    "(warehouse_id INTEGER NOT NULL, item_id INTEGER NOT NULL, work_id TEXT NOT NULL UNIQUE, "
                    "PRIMARY KEY(warehouse_id,item_id))") ||
        !query.exec("CREATE TABLE IF NOT EXISTS behavior_events "
                    "(user_id INTEGER NOT NULL REFERENCES network_users(id), event_id TEXT NOT NULL, "
                    "action TEXT NOT NULL, query TEXT NOT NULL, occurred_at_utc INTEGER NOT NULL, "
                    "synthetic INTEGER NOT NULL DEFAULT 0, public_eligible INTEGER NOT NULL DEFAULT 0, "
                    "work_id TEXT, warehouse_id INTEGER, item_id INTEGER, media_snapshot BLOB, "
                    "PRIMARY KEY(user_id,event_id))") ||
        !query.exec("CREATE INDEX IF NOT EXISTS behavior_events_user_time ON behavior_events(user_id,occurred_at_utc)") ||
        !query.exec("CREATE INDEX IF NOT EXISTS behavior_events_public_time ON behavior_events(public_eligible,action,occurred_at_utc)") ||
        !query.exec("CREATE INDEX IF NOT EXISTS behavior_events_public_source ON behavior_events(warehouse_id,item_id,work_id,public_eligible,action,occurred_at_utc,user_id)") ||
        !query.exec("CREATE TABLE IF NOT EXISTS behavior_settings "
                    "(user_id INTEGER PRIMARY KEY REFERENCES network_users(id), history_enabled INTEGER NOT NULL, "
                    "personalization_enabled INTEGER NOT NULL, contribute_public_trends INTEGER NOT NULL)") ||
        !query.exec("CREATE TABLE IF NOT EXISTS work_feedback "
                    "(user_id INTEGER NOT NULL REFERENCES network_users(id), work_id TEXT NOT NULL, "
                    "warehouse_id INTEGER NOT NULL, item_id INTEGER NOT NULL, PRIMARY KEY(user_id,work_id))") ||
        !query.exec("CREATE TABLE IF NOT EXISTS server_schema_migrations(version INTEGER PRIMARY KEY)") ||
        !query.exec("INSERT OR IGNORE INTO server_schema_migrations(version) VALUES(1)") ||
        !query.exec("CREATE TABLE IF NOT EXISTS network_user_access (user_id INTEGER PRIMARY KEY "
                    "REFERENCES network_users(id), is_admin INTEGER NOT NULL DEFAULT 0 CHECK(is_admin IN (0,1)), "
                    "disabled INTEGER NOT NULL DEFAULT 0 CHECK(disabled IN (0,1)))") ||
        !query.exec("CREATE TABLE IF NOT EXISTS admin_audit (id INTEGER PRIMARY KEY AUTOINCREMENT, "
                    "actor_id INTEGER NOT NULL, target_id INTEGER NOT NULL, action TEXT NOT NULL, "
                    "occurred_at_utc INTEGER NOT NULL)") ||
        !person_schema::migrate(db_, error) ||
        // >>> MAIN-DEFENSE | openDatabase｜处理阶段 04
        // 提交这一批修改；必须检查提交结果，调用了commit不等于已经成功。
        // <<< MAIN-DEFENSE
        !db_.commit()) {
        db_.rollback();
        if (error.isEmpty()) error = QStringLiteral("无法创建仓库表。");
        return false;
    }
    personStore_ = std::make_unique<person_store::SqliteStore>(db_);
    // >>> MAIN-DEFENSE | openDatabase｜处理阶段 05
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!personContextLoader_) {
        personContextLoader_ = [this](qint64 userId, const QList<care_contract::ResourceRef>& resources) {
            return personStore_->contextFor(userId, resources);
        };
    }
    // >>> MAIN-DEFENSE | openDatabase｜处理阶段 06
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return true;
}

bool WarehouseServer::addUser(const QString& username, const QString& password, QString& error, bool administrator, qint64 actorId)
{
    return createUser(username, password, error, administrator, actorId, false);
}

std::optional<bool> WarehouseServer::hasAdministrator(QString& error) const
{
    QSqlQuery query(db_);
    if (!query.exec("SELECT EXISTS(SELECT 1 FROM network_user_access WHERE is_admin=1)") || !query.next()) {
        error = QStringLiteral("无法检查管理员状态。");
        return std::nullopt;
    }
    return query.value(0).toBool();
}

bool WarehouseServer::initializeAdministrator(const QString& username, const QString& password, QString& error)
{
    return createUser(username, password, error, true, 0, true);
}

bool WarehouseServer::createUser(const QString& username, const QString& password, QString& error,
                                 bool administrator, qint64 actorId, bool firstAdministratorOnly)
// >>> MAIN-DEFENSE | 函数入口｜WarehouseServer::addUser
// 创建在线network_users账号并哈希密码；在线仓库权限按所有者，不使用单机admin角色替代。
// 签名提示：bool WarehouseServer::addUser(const QString& username, const QString& password, QString& error)

// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
{
    const auto key = userKey(username);
    const auto bytes = password.toUtf8();
    // >>> MAIN-DEFENSE | addUser｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (key.isEmpty() || username.size() > 64) {
        error = QStringLiteral("用户名不能为空，最多 64 个字符。");
        return false;
    }
    error = online_password::validationError(password);
    if (!error.isEmpty()) return false;
    char hash[crypto_pwhash_STRBYTES];
    // >>> MAIN-DEFENSE | addUser｜处理阶段 02
    // 生成密码哈希：明文只用于本次计算，保存带盐哈希；这不是可逆加密。
    // <<< MAIN-DEFENSE
    if (crypto_pwhash_str(hash, bytes.constData(), bytes.size(), crypto_pwhash_OPSLIMIT_INTERACTIVE,
                          crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0) {
        error = QStringLiteral("无法生成密码哈希。");
        return false;
    }
    if (!db_.transaction()) { error = QStringLiteral("无法开始账号事务。"); return false; }
    if (firstAdministratorOnly) {
        // Count and insert share one transaction; concurrent bootstrap cannot create two admins.
        const auto exists = hasAdministrator(error);
        if (!exists || *exists) {
            db_.rollback();
            if (exists) error = QStringLiteral("此数据库已有管理员，不能重复初始化。请使用已有账号登录。");
            return false;
        }
    }
    QSqlQuery query(db_);
    // >>> MAIN-DEFENSE | addUser｜处理阶段 03
    // 准备参数化SQL，涉及 network_users。随后把实际参数绑定到问号，执行并检查错误；prepare本身尚未完成读写。
    // <<< MAIN-DEFENSE
    query.prepare("INSERT INTO network_users(username,username_key,password_hash) VALUES (?,?,?)");
    query.addBindValue(username.trimmed());
    query.addBindValue(key);
    query.addBindValue(QString::fromLatin1(hash));
    // >>> MAIN-DEFENSE | addUser｜处理阶段 04
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!query.exec()) {
        db_.rollback();
        error = QStringLiteral("无法添加网络账号，请检查用户名是否已存在。");
        return false;
    }
    const auto newId = query.lastInsertId();
    query.prepare("INSERT INTO network_user_access(user_id,is_admin) VALUES (?,?)");
    query.addBindValue(newId); query.addBindValue(administrator);
    bool saved = query.exec();
    if (saved && (actorId > 0 || firstAdministratorOnly)) {
        query.prepare("INSERT INTO admin_audit(actor_id,target_id,action,occurred_at_utc) VALUES (?,?,?,?)");
        query.addBindValue(actorId > 0 ? QVariant(actorId) : newId); query.addBindValue(newId);
        query.addBindValue(firstAdministratorOnly ? "initialize_admin" : "create_user");
        query.addBindValue(QDateTime::currentSecsSinceEpoch());
        saved = query.exec();
    }
    if (!saved || !db_.commit()) {
        db_.rollback(); error = QStringLiteral("无法保存账号权限。"); return false;
    }
    // >>> MAIN-DEFENSE | addUser｜处理阶段 05
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return true;
}

// >>> MAIN-DEFENSE | 函数入口｜WarehouseServer::readableCandidates
// 只整理当前用户可读的仓库条目，供推荐使用，不把别人私有数据传给模型。
// 签名提示：std::optional<QList<WarehouseDetail>> WarehouseServer::readableCandidates(qint64 userId)

// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
std::optional<QList<WarehouseDetail>> WarehouseServer::readableCandidates(qint64 userId)
{
    if (userId <= 0)
        return std::nullopt;
    QSqlQuery query(db_);
    // >>> MAIN-DEFENSE | readableCandidates｜处理阶段 01
    // 准备参数化SQL，涉及 warehouses、network_users。随后把实际参数绑定到问号，执行并检查错误；prepare本身尚未完成读写。
    // <<< MAIN-DEFENSE
    query.prepare("SELECT id,name,owner_id,is_public FROM warehouses "
                  "WHERE EXISTS(SELECT 1 FROM network_users WHERE id=?) AND (owner_id=? OR is_public=1) ORDER BY id");
    query.addBindValue(userId);
    query.addBindValue(userId);
    // >>> MAIN-DEFENSE | readableCandidates｜处理阶段 02
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!query.exec())
        return std::nullopt;
    QList<WarehouseDetail> rows;
    SnapshotBudget budget;
    // >>> MAIN-DEFENSE | readableCandidates｜处理阶段 03
    // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
    // <<< MAIN-DEFENSE
    while (query.next()) {
        if (rows.size() >= 1000) return std::nullopt;
        const auto warehouse = readSummary(query);
        auto items = listItems(db_, warehouse.warehouseId, &budget);
        // >>> MAIN-DEFENSE | readableCandidates｜处理阶段 04
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!items)
            return std::nullopt;
        rows.append({warehouse, std::move(*items)});
    }
    // >>> MAIN-DEFENSE | readableCandidates｜处理阶段 05
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return rows;
}

// >>> MAIN-DEFENSE | 函数入口｜WarehouseServer::sendResponse
// 将业务结果编码为协议帧并写回当前socket。
// 签名提示：bool WarehouseServer::sendResponse(QTcpSocket* socket, const QJsonObject& response)

// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool WarehouseServer::sendResponse(QTcpSocket* socket, const QJsonObject& response)
{
    QString error;
    auto frame = warehouse_protocol::encodeFrame(response, error);
    // >>> MAIN-DEFENSE | sendResponse｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (frame.isEmpty())
        frame = warehouse_protocol::encodeFrame({{"type", "error_resp"}, {"req_id", response.value("req_id")},
                {"code", "ServerError"}, {"message", QStringLiteral("响应超过帧大小限制。")}}, error);
    if (socket->state() != QAbstractSocket::ConnectedState) return false;
    // >>> MAIN-DEFENSE | sendResponse｜处理阶段 02
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (frame.isEmpty() || socket->bytesToWrite() > warehouse_protocol::MaxFrameSize || socket->write(frame) != frame.size()) {
        socket->abort();
        return false;
    }
    // >>> MAIN-DEFENSE | sendResponse｜处理阶段 03
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return true;
}

// >>> MAIN-DEFENSE | 函数入口｜WarehouseServer::queryBehavior
// 按用户与时间窗读服务器行为记录，保存读取失败状态和历史快照。
// 签名提示：std::optional<QList<StoredBehavior>> WarehouseServer::queryBehavior(qint64 userId, care_contract::TimeRange range)

// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
std::optional<QList<StoredBehavior>> WarehouseServer::queryBehavior(qint64 userId, care_contract::TimeRange range)
{
    if (userId <= 0 || !care_contract::validate(range).ok()) return std::nullopt;
    QSqlQuery exists(db_);
    // >>> MAIN-DEFENSE | queryBehavior｜处理阶段 01
    // 准备参数化SQL，涉及 network_users、behavior_events。随后把实际参数绑定到问号，执行并检查错误；prepare本身尚未完成读写。
    // <<< MAIN-DEFENSE
    exists.prepare("SELECT 1 FROM network_users WHERE id=?");
    exists.addBindValue(userId);
    if (!exists.exec() || !exists.next()) return std::nullopt;
    QSqlQuery query(db_);
    // >>> MAIN-DEFENSE | queryBehavior｜处理阶段 02
    // 准备参数化SQL，涉及 behavior_events。随后把实际参数绑定到问号，执行并检查错误；prepare本身尚未完成读写。
    // <<< MAIN-DEFENSE
    query.prepare("SELECT event_id,action,query,occurred_at_utc,synthetic,work_id,warehouse_id,item_id,media_snapshot,public_eligible "
                  "FROM behavior_events WHERE user_id=? AND occurred_at_utc>=? AND occurred_at_utc<? "
                  "ORDER BY occurred_at_utc,event_id");
    query.addBindValue(userId); query.addBindValue(range.beginUtc); query.addBindValue(range.endUtc);
    // >>> MAIN-DEFENSE | queryBehavior｜处理阶段 03
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!query.exec()) return std::nullopt;
    QList<StoredBehavior> result;
    qint64 snapshotBytes = 0;
    while (query.next()) {
        // >>> MAIN-DEFENSE | queryBehavior｜处理阶段 04
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (result.size() >= 10000) return std::nullopt;
        StoredBehavior stored;
        care_contract::BehaviorRecord record;
        record.event.eventId = query.value(0).toString();
        const auto action = query.value(1).toString();
        // >>> MAIN-DEFENSE | queryBehavior｜处理阶段 05
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (action == "view") {
            record.event.action = care_contract::BehaviorAction::View;
            record.event.resource = care_contract::ResourceRef{query.value(5).toString(),
                {query.value(6).toLongLong(), query.value(7).toLongLong()}};
            const auto bytes = query.value(8).toByteArray();
            snapshotBytes += bytes.size();
            // >>> MAIN-DEFENSE | queryBehavior｜处理阶段 06
            // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
            // <<< MAIN-DEFENSE
            if (snapshotBytes > 16 * 1024 * 1024) return std::nullopt;
            MediaRecord media; QString error;
            const auto object = QJsonDocument::fromJson(bytes).object();
            if (!media_json::fromJson(object, media, error)) return std::nullopt;
            const auto people = person_snapshot_json::decode(object, *record.event.resource, media);
            // >>> MAIN-DEFENSE | queryBehavior｜处理阶段 07
            // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
            // <<< MAIN-DEFENSE
            if (!people) return std::nullopt;
            stored.peopleSnapshot = *people;
            stored.mediaSnapshot = media;
        } else if (action == "search") {
            record.event.action = care_contract::BehaviorAction::Search;
            record.event.query = query.value(2).toString();
        } else return std::nullopt;
        record.occurredAtUtc = query.value(3).toLongLong();
        record.synthetic = query.value(4).toBool();
        // >>> MAIN-DEFENSE | queryBehavior｜处理阶段 08
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!care_contract::validate(record.event).ok()) return std::nullopt;
        stored.record = record;
        stored.publicEligible = query.value(9).toBool();
        result.append(stored);
    }
    // >>> MAIN-DEFENSE | queryBehavior｜处理阶段 09
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (query.lastError().isValid()) return std::nullopt;
    return result;
}

// >>> MAIN-DEFENSE | 函数入口｜WarehouseServer::loadPersonContext
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：care_contract::Result<care_algorithms::PersonContext> WarehouseServer::loadPersonContext(
//       qint64 userId, const QList<care_contract::ResourceSummary>& authorizedWorks)

// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
care_contract::Result<care_algorithms::PersonContext> WarehouseServer::loadPersonContext(
    qint64 userId, const QList<care_contract::ResourceSummary>& authorizedWorks)
{
    if (!personContextLoader_) return {{}, care_algorithms::PersonContext{}};
    QList<care_contract::ResourceRef> refs;
    refs.reserve(authorizedWorks.size());
    // >>> MAIN-DEFENSE | loadPersonContext｜处理阶段 01
    // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
    // <<< MAIN-DEFENSE
    for (const auto& work : authorizedWorks) refs.append(work.resource);
    return personContextLoader_(userId, refs);
}

// >>> MAIN-DEFENSE | 函数入口｜WarehouseServer::personCatalog
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：care_contract::Result<care_algorithms::CatalogSnapshot> WarehouseServer::personCatalog(
//       qint64 userId, const QList<care_contract::ResourceSummary>& authorizedWorks)

// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
care_contract::Result<care_algorithms::CatalogSnapshot> WarehouseServer::personCatalog(
    qint64 userId, const QList<care_contract::ResourceSummary>& authorizedWorks)
{
    const auto context = loadPersonContext(userId, authorizedWorks);
    // >>> MAIN-DEFENSE | personCatalog｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!context.status.ok() || !context.value)
        return {{ErrorCode::ServerError, QStringLiteral("人物目录读取失败。")}, {}};
    care_algorithms::CatalogSnapshot catalog;
    catalog.readableWorks = authorizedWorks;
    // >>> MAIN-DEFENSE | personCatalog｜处理阶段 02
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return care_algorithms::attachConfirmedPeople(catalog, *context.value);
}

// >>> MAIN-DEFENSE | 函数入口｜WarehouseServer::readRequests
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：void WarehouseServer::readRequests(QTcpSocket* socket, const std::shared_ptr<Session>& session)

// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
void WarehouseServer::readRequests(QTcpSocket* socket, const std::shared_ptr<Session>& session)
{
    QList<QJsonObject> requests;
    QString error;
    const bool valid = session->decoder.feed(socket->readAll(), requests, error);
    // >>> MAIN-DEFENSE | readRequests｜处理阶段 01
    // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
    // <<< MAIN-DEFENSE
    for (const auto& request : requests) {
        if (socket->state() != QAbstractSocket::ConnectedState) return;
        if (!request.value("req_id").isString() || request.value("req_id").toString().trimmed().isEmpty() ||
            request.value("req_id").toString().size() > 128) {
            socket->disconnectFromHost();
            return;
        }
        const QString id = request.value("req_id").toString();
        // >>> MAIN-DEFENSE | readRequests｜处理阶段 02
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (session->pending.contains(id) || session->summaries.contains(id) || session->scenes.contains(id) || session->personLookup == id) {
            socket->abort();
            return;
        }
        const auto type = request.value("type").toString();
        const auto scene = request.value("payload").toObject().value("scene").toString();
        // >>> MAIN-DEFENSE | readRequests｜处理阶段 03
        // 请求分支：recommend_temporal。当前session提供身份；先验证参数和操作权限，再执行该分支。
        // <<< MAIN-DEFENSE
        if (type == "recommend_temporal" && (scene == "relax" || scene == "focus" || scene == "explore")) {
            startSceneRecommendation(socket, session, request);
            continue;
        }
        // >>> MAIN-DEFENSE | readRequests｜处理阶段 04
        // 请求分支：search_person_web。当前session提供身份；先验证参数和操作权限，再执行该分支。
        // <<< MAIN-DEFENSE
        if (type == "search_person_web") {
            startPersonLookup(socket, session, request);
            continue;
        }
        // >>> MAIN-DEFENSE | readRequests｜处理阶段 05
        // 请求分支：get_trending。当前session提供身份；先验证参数和操作权限，再执行该分支。
        // <<< MAIN-DEFENSE
        if (type == "get_trending" && request.value("payload").toObject().value("summary_mode") == QJsonValue("model")) {
            startTrendingSummary(socket, session, request);
            continue;
        }
        // >>> MAIN-DEFENSE | readRequests｜处理阶段 06
        // 请求分支：recommend_resources。当前session提供身份；先验证参数和操作权限，再执行该分支。
        // <<< MAIN-DEFENSE
        if (type == "recommend_resources" || type == "cancel_recommendation") {
            startRecommendation(socket, session, request);
            continue;
        }
        // >>> MAIN-DEFENSE | readRequests｜处理阶段 07
        // 请求分支：logout。当前session提供身份；先验证参数和操作权限，再执行该分支。
        // <<< MAIN-DEFENSE
        if (type == "logout" && session->userId && request.value("token").toString() == session->token &&
            request.value("payload").isObject())
            cancelPending(*session, socket, true);
        if (socket->state() != QAbstractSocket::ConnectedState) return;
        // >>> MAIN-DEFENSE | readRequests｜处理阶段 08
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!sendResponse(socket, dispatch(*session, request))) return;
    }
    if (!valid)
        socket->disconnectFromHost();
}

// >>> MAIN-DEFENSE | 函数入口｜WarehouseServer::dispatch
// 按请求type分发同步业务，使用当前session校验身份与权限后访问数据库或算法。
// 签名提示：QJsonObject WarehouseServer::dispatch(Session& session, const QJsonObject& request)

// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
QJsonObject WarehouseServer::dispatch(Session& session, const QJsonObject& request)
{
    const auto id = request.value("req_id");
    const QString type = request.value("type").toString();
    const QJsonObject payload = request.value("payload").toObject();
    const auto fail = [&](const char* code, const QString& message) {
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 01
        // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
        // <<< MAIN-DEFENSE
        return QJsonObject{{"type", "error_resp"}, {"req_id", id}, {"code", code}, {"message", message}};
    };
    const auto success = [&](const QJsonObject& result = {}) {
        return QJsonObject{{"type", type + "_resp"}, {"req_id", id}, {"payload", result}};
    };
    const auto invalid = [&] { return fail("InvalidArgument", QStringLiteral("请求参数无效。")); };
    const auto dbError = [&] { return fail("ServerError", QStringLiteral("仓库数据库操作失败。")); };
    // >>> MAIN-DEFENSE | dispatch｜处理阶段 02
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (type.isEmpty() || !request.value("payload").isObject())
        return invalid();

    QSqlQuery query(db_);
    // >>> MAIN-DEFENSE | dispatch｜处理阶段 03
    // 请求分支：login。当前session提供身份；先验证参数和操作权限，再执行该分支。
    // <<< MAIN-DEFENSE
    if (type == "login") {
        if (!request.value("token").isUndefined() || !payload.value("username").isString() ||
            !payload.value("password").isString() || payload.value("username").toString().size() > 64)
            return invalid();
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 04
        // 使用当前连接的会话身份检查或构造业务上下文；客户端传来的编号不能替代登录身份。
        // <<< MAIN-DEFENSE
        if (session.userId)
            return invalid();
        const auto role = payload.value("login_role");
        if (!role.isUndefined() && (!role.isString() || (role != "user" && role != "admin")))
            return invalid();
        const auto username = userKey(payload.value("username").toString());
        const auto password = payload.value("password").toString().toUtf8();
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 05
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (username.isEmpty() || password.isEmpty() || password.size() > 1024)
            return invalid();
        query.prepare("SELECT u.id,u.username,u.password_hash,COALESCE(a.is_admin,0),COALESCE(a.disabled,0) FROM network_users u LEFT JOIN network_user_access a ON a.user_id=u.id WHERE u.username_key=?");
        query.addBindValue(username);
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 06
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!query.exec())
            return dbError();
        if (!query.next() || crypto_pwhash_str_verify(query.value(2).toString().toLatin1().constData(),
                                                     password.constData(), password.size()) != 0)
            // >>> MAIN-DEFENSE | dispatch｜处理阶段 07
            // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
            // <<< MAIN-DEFENSE
            return fail("AuthenticationFailed", QStringLiteral("用户名或密码不正确。"));
        if (query.value(4).toBool())
            return fail("AuthenticationFailed", QStringLiteral("此账号已被停用，请联系管理员。"));
        if (!role.isUndefined() && query.value(3).toBool() != (role == "admin"))
            return fail("PermissionDenied", QStringLiteral("账号角色与登录入口不匹配，请返回选择对应入口。"));
        session.userId = query.value(0).toLongLong();
        QByteArray token(32, Qt::Uninitialized);
        randombytes_buf(token.data(), token.size());
        session.token = QString::fromLatin1(token.toHex());
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 08
        // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
        // <<< MAIN-DEFENSE
        return success({{"token", session.token}, {"user", QJsonObject{
            {"user_id", QString::number(session.userId)}, {"username", query.value(1).toString()}, {"is_admin", query.value(3).toBool()}}}});
    }

    // >>> MAIN-DEFENSE | dispatch｜处理阶段 09
    // 使用当前连接的会话身份检查或构造业务上下文；客户端传来的编号不能替代登录身份。
    // <<< MAIN-DEFENSE
    if (!session.userId || request.value("token").toString() != session.token)
        return fail("NotAuthenticated", QStringLiteral("请在当前连接登录。"));
    if (type == "logout") {
        session.userId = 0;
        session.token.clear();
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 10
        // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
        // <<< MAIN-DEFENSE
        return success();
    }
    if (type.startsWith("admin_")) return dispatchAdmin(session, request);
    if (const auto person = dispatchPersonQuery(session.userId, type, payload)) {
        if (person->status.ok() && person->value)
            // >>> MAIN-DEFENSE | dispatch｜处理阶段 11
            // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
            // <<< MAIN-DEFENSE
            return success(*person->value);
        switch (person->status.code) {
        case ErrorCode::InvalidArgument: return fail("InvalidArgument", person->status.message);
        case ErrorCode::PermissionDenied: return fail("PermissionDenied", person->status.message);
        case ErrorCode::NotFound: return fail("NotFound", person->status.message);
        default: return fail("ServerError", person->status.message);
        }
    }
    // >>> MAIN-DEFENSE | dispatch｜处理阶段 12
    // 请求分支：set_work_feedback。当前session提供身份；先验证参数和操作权限，再执行该分支。
    // <<< MAIN-DEFENSE
    if (type == "set_work_feedback") {
        care_contract::ResourceRef ref;
        if (!readResource(payload.value("resource"), ref) || !payload.value("feedback").isString()) return invalid();
        const auto feedback = payload.value("feedback").toString();
        QSqlQuery query(db_);
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 13
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (feedback == "neutral") {
            // Removing one's own stored feedback remains possible after source deletion.
            query.prepare("DELETE FROM work_feedback WHERE user_id=? AND work_id=?");
            query.addBindValue(session.userId); query.addBindValue(ref.workId);
            // >>> MAIN-DEFENSE | dispatch｜处理阶段 14
            // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
            // <<< MAIN-DEFENSE
            if (!query.exec()) return dbError();
            return success();
        }
        if (feedback != "not_interested") return invalid();
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 15
        // 准备参数化SQL，涉及 warehouses、warehouse_items、work_sources。随后把实际参数绑定到问号，执行并检查错误；prepare本身尚未完成读写。
        // <<< MAIN-DEFENSE
        query.prepare("SELECT w.owner_id,w.is_public,s.work_id FROM warehouses w JOIN warehouse_items i ON i.warehouse_id=w.id "
                      "JOIN work_sources s ON s.warehouse_id=w.id AND s.item_id=i.id WHERE w.id=? AND i.id=?");
        query.addBindValue(ref.source.warehouseId); query.addBindValue(ref.source.itemId);
        if (!query.exec()) return dbError();
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 16
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!query.next()) return query.lastError().isValid() ? dbError() : fail("NotFound", QStringLiteral("资源不存在。"));
        if (query.value(0).toLongLong() != session.userId && !query.value(1).toBool())
            return fail("PermissionDenied", QStringLiteral("没有此资源的访问权限。"));
        if (query.value(2).toString() != ref.workId) return invalid();
        query.finish();
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 17
        // 准备参数化SQL，涉及 work_feedback、SET。随后把实际参数绑定到问号，执行并检查错误；prepare本身尚未完成读写。
        // <<< MAIN-DEFENSE
        query.prepare("INSERT INTO work_feedback(user_id,work_id,warehouse_id,item_id) VALUES(?,?,?,?) "
                      "ON CONFLICT(user_id,work_id) DO UPDATE SET warehouse_id=excluded.warehouse_id,item_id=excluded.item_id");
        query.addBindValue(session.userId); query.addBindValue(ref.workId);
        query.addBindValue(ref.source.warehouseId); query.addBindValue(ref.source.itemId);
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 18
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!query.exec()) return dbError();
        return success();
    }
    if (type == "list_work_feedback") {
        qint64 offset = 0;
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 19
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!readUtc(payload.value("offset"), offset) || !payload.value("limit").isDouble()) return invalid();
        const double limit = payload.value("limit").toDouble();
        if (limit < 1 || limit > care_contract::MaxPageSize || limit != static_cast<int>(limit)) return invalid();
        const care_contract::PageRequest page{offset, static_cast<int>(limit)};
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 20
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!care_contract::validate(page).ok()) return invalid();
        QSqlQuery query(db_);
        query.prepare("SELECT f.work_id,s.warehouse_id,s.item_id,i.media FROM work_feedback f "
                      "JOIN work_sources s ON s.work_id=f.work_id "
                      "JOIN warehouse_items i ON i.id=s.item_id AND i.warehouse_id=s.warehouse_id "
                      "JOIN warehouses w ON w.id=s.warehouse_id WHERE f.user_id=? AND (w.owner_id=? OR w.is_public=1) "
                      "ORDER BY f.work_id,s.warehouse_id,s.item_id LIMIT ? OFFSET ?");
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 21
        // 使用当前连接的会话身份检查或构造业务上下文；客户端传来的编号不能替代登录身份。
        // <<< MAIN-DEFENSE
        query.addBindValue(session.userId); query.addBindValue(session.userId);
        query.addBindValue(page.limit + 1); query.addBindValue(page.offset);
        if (!query.exec()) return dbError();
        QJsonArray items; bool more = false;
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 22
        // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
        // <<< MAIN-DEFENSE
        while (query.next()) {
            if (items.size() == page.limit) { more = true; break; }
            MediaRecord media; QString error;
            if (!media_json::fromJson(QJsonDocument::fromJson(query.value(3).toByteArray()).object(), media, error)) return dbError();
            care_contract::ResourceSummary work{{query.value(0).toString(), {query.value(1).toLongLong(), query.value(2).toLongLong()}}, media, {}};
            items.append(QJsonObject{{"work", workJson(work)}, {"feedback", "not_interested"}});
        }
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 23
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (query.lastError().isValid()) return dbError();
        QJsonObject result{{"items", items}};
        if (more) result.insert("next_offset", QString::number(page.offset + page.limit));
        return success({{"page", result}});
    }
    // >>> DEFENSE-NOTE | 共享接线｜时序推荐规则基线
    // 从session识别用户，解析偏好/scene/limit并读取已授权候选和本人历史。
    // 构造PersonalSnapshot、参与设置、完整公共热度与排除项，再交给care_algorithms::recommendTemporal。
    // 新场景路由复用该规则基线，模型筛选在scene_recommendation_router.cpp，不能把两层误说成一个公式。
    // <<< DEFENSE-NOTE | 本段讲解结束
    // >>> MAIN-DEFENSE | dispatch｜处理阶段 24
    // 请求分支：recommend_temporal。当前session提供身份；先验证参数和操作权限，再执行该分支。
    // <<< MAIN-DEFENSE
    if (type == "recommend_temporal") {
        care_contract::TemporalRecommendationRequest input;
        if (!readPreference(payload.value("category"), input.category) || !readPreference(payload.value("file_type"), input.fileType) ||
            !payload.value("scene").isString() || !payload.value("limit").isDouble()) return invalid();
        const auto scene = payload.value("scene").toString();
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 25
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (scene == "neutral") input.scene = care_contract::BrowseScene::Neutral;
        else if (scene == "relax") input.scene = care_contract::BrowseScene::Relax;
        else if (scene == "focus") input.scene = care_contract::BrowseScene::Focus;
        else if (scene == "explore") input.scene = care_contract::BrowseScene::Explore;
        else return invalid();
        const double limit = payload.value("limit").toDouble();
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 26
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (limit < 1 || limit > care_contract::MaxRecommendationLimit || limit != static_cast<int>(limit)) return invalid();
        input.limit = static_cast<int>(limit);
        if (!care_contract::validate(input).ok()) return invalid();
        const auto candidates = readableCandidates(session.userId);
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 27
        // 使用当前连接的会话身份检查或构造业务上下文；客户端传来的编号不能替代登录身份。
        // <<< MAIN-DEFENSE
        const auto history = queryBehavior(session.userId, {0, care_contract::MaxUtcExclusive});
        if (!candidates || !history) return dbError();
        care_algorithms::PersonalSnapshot snapshot;
        snapshot.userKey = QString::number(session.userId);
        snapshot.nowUtc = QDateTime::currentSecsSinceEpoch();
        QSqlQuery exclusions(db_);
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 28
        // 准备参数化SQL，涉及 work_feedback、work_sources、warehouse_items、warehouses。随后把实际参数绑定到问号，执行并检查错误；prepare本身尚未完成读写。
        // <<< MAIN-DEFENSE
        exclusions.prepare("SELECT f.work_id FROM work_feedback f "
            "JOIN work_sources s ON s.work_id=f.work_id "
            "JOIN warehouse_items i ON i.id=s.item_id AND i.warehouse_id=s.warehouse_id "
            "JOIN warehouses w ON w.id=s.warehouse_id WHERE f.user_id=? AND (w.owner_id=? OR w.is_public=1)");
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 29
        // 使用当前连接的会话身份检查或构造业务上下文；客户端传来的编号不能替代登录身份。
        // <<< MAIN-DEFENSE
        exclusions.addBindValue(session.userId); exclusions.addBindValue(session.userId);
        if (!exclusions.exec()) return dbError();
        while (exclusions.next()) {
            if (snapshot.notInterestedWorkIds.size() >= 10000) return dbError();
            snapshot.notInterestedWorkIds.insert(exclusions.value(0).toString());
        }
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 30
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (exclusions.lastError().isValid()) return dbError();
        QSqlQuery settings(db_);
        settings.prepare("SELECT history_enabled,personalization_enabled,contribute_public_trends FROM behavior_settings WHERE user_id=?");
        settings.addBindValue(session.userId);
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 31
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!settings.exec()) return dbError();
        if (settings.next()) snapshot.settings = {settings.value(0).toBool(), settings.value(1).toBool(), settings.value(2).toBool()};
        else if (settings.lastError().isValid()) return dbError();
        // One transaction avoids one disk commit per newly discovered candidate.
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 32
        // 开始数据库事务；下方相关写入必须一起成功，失败路径执行回滚。
        // <<< MAIN-DEFENSE
        if (!db_.transaction()) return dbError();
        QSqlQuery insertSource(db_), findSource(db_);
        insertSource.prepare("INSERT OR IGNORE INTO work_sources(warehouse_id,item_id,work_id) VALUES(?,?,?)");
        findSource.prepare("SELECT work_id FROM work_sources WHERE warehouse_id=? AND item_id=?");
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 33
        // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
        // <<< MAIN-DEFENSE
        for (const auto& warehouse : *candidates) {
            for (const auto& item : warehouse.items) {
                findSource.bindValue(0, warehouse.warehouse.warehouseId); findSource.bindValue(1, item.itemId);
                if (!findSource.exec()) { db_.rollback(); return dbError(); }
                // >>> MAIN-DEFENSE | dispatch｜处理阶段 34
                // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
                // <<< MAIN-DEFENSE
                if (!findSource.next()) {
                    if (findSource.lastError().isValid()) { db_.rollback(); return dbError(); }
                    findSource.finish();
                    insertSource.bindValue(0, warehouse.warehouse.warehouseId); insertSource.bindValue(1, item.itemId);
                    insertSource.bindValue(2, QStringLiteral("srv:") + QUuid::createUuid().toString(QUuid::WithoutBraces));
                    // >>> MAIN-DEFENSE | dispatch｜处理阶段 35
                    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
                    // <<< MAIN-DEFENSE
                    if (!insertSource.exec() || !findSource.exec() || !findSource.next()) { db_.rollback(); return dbError(); }
                }
                snapshot.readableWorks.append({{findSource.value(0).toString(), {warehouse.warehouse.warehouseId, item.itemId}}, item.media, {}});
                findSource.finish();
            }
        }
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 36
        // 提交这一批修改；必须检查提交结果，调用了commit不等于已经成功。
        // <<< MAIN-DEFENSE
        if (!db_.commit()) { db_.rollback(); return dbError(); }
        for (const auto& stored : *history) {
            care_algorithms::ObservedBehavior event;
            event.userKey = snapshot.userKey; event.record = stored.record; event.publicEligible = stored.publicEligible;
            // >>> MAIN-DEFENSE | dispatch｜处理阶段 37
            // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
            // <<< MAIN-DEFENSE
            if (stored.record.event.resource && stored.mediaSnapshot)
                event.workSnapshot = care_contract::ResourceSummary{*stored.record.event.resource, *stored.mediaSnapshot, stored.peopleSnapshot};
            snapshot.events.append(event);
        }
        // The recommendation uses the complete seven-day public heat window, not the truncated UI top list.
        const QTimeZone zone("Asia/Shanghai");
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 38
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!zone.isValid()) return dbError();
        QMap<QString, QMap<qint64, QSet<qint64>>> heatDays;
        QSet<QString> syntheticHeat;
        QSqlQuery heat(db_);
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 39
        // 准备参数化SQL，涉及 behavior_events、behavior_settings、work_sources、warehouse_items、warehouses。随后把实际参数绑定到问号，执行并检查错误；prepare本身尚未完成读写。
        // <<< MAIN-DEFENSE
        heat.prepare("SELECT e.work_id,e.user_id,e.occurred_at_utc,e.synthetic FROM behavior_events e "
            "JOIN behavior_settings b ON b.user_id=e.user_id JOIN work_sources s ON s.work_id=e.work_id "
            "AND s.warehouse_id=e.warehouse_id AND s.item_id=e.item_id "
            "JOIN warehouse_items i ON i.id=s.item_id AND i.warehouse_id=s.warehouse_id "
            "JOIN warehouses w ON w.id=s.warehouse_id WHERE w.is_public=1 AND e.public_eligible=1 "
            "AND b.contribute_public_trends=1 AND e.action='view' AND e.occurred_at_utc>=? AND e.occurred_at_utc<=?");
        heat.addBindValue(qMax<qint64>(0, snapshot.nowUtc - 7 * 86400)); heat.addBindValue(snapshot.nowUtc);
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 40
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!heat.exec()) return dbError();
        int heatRows = 0;
        while (heat.next()) {
            if (++heatRows > 100000) return dbError();
            const auto workId = heat.value(0).toString(); const auto userId = heat.value(1).toLongLong();
            const auto date = QDateTime::fromSecsSinceEpoch(heat.value(2).toLongLong(), zone).date();
            // >>> MAIN-DEFENSE | dispatch｜处理阶段 41
            // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
            // <<< MAIN-DEFENSE
            if (workId.isEmpty() || userId <= 0 || !date.isValid()) return dbError();
            heatDays[workId][userId].insert(date.toJulianDay());
            if (heat.value(3).toBool()) syntheticHeat.insert(workId);
        }
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 42
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (heat.lastError().isValid()) return dbError();
        for (auto it = heatDays.cbegin(); it != heatDays.cend(); ++it) {
            if (it.value().size() < 3) continue;
            qint64 effective = 0;
            // >>> MAIN-DEFENSE | dispatch｜处理阶段 43
            // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
            // <<< MAIN-DEFENSE
            for (const auto& days : it.value()) effective += days.size();
            snapshot.publicHeat.insert(it.key(), effective);
            if (syntheticHeat.contains(it.key())) snapshot.publicHeatContainsSyntheticData = true;
        }
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 44
        // 使用当前连接的会话身份检查或构造业务上下文；客户端传来的编号不能替代登录身份。
        // <<< MAIN-DEFENSE
        const auto context = loadPersonContext(session.userId, snapshot.readableWorks);
        if (!context.status.ok() || !context.value) return dbError();
        const auto enriched = care_algorithms::attachConfirmedPeople(snapshot, *context.value);
        if (!enriched.status.ok() || !enriched.value) return dbError();
        const auto recommended = care_algorithms::recommendTemporal(input, *enriched.value);
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 45
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!recommended.status.ok() || !recommended.value) return fail("ServerError", recommended.status.message);
        QJsonArray rows;
        for (const auto& item : recommended.value->items) rows.append(QJsonObject{{"work", workJson(item.work)}, {"reason", item.reason}});
        const QString note = personContextLoader_ ? QStringLiteral("人物和关注只使用服务端确认的关联。") :
            QStringLiteral("人物目录尚未配置，当前使用历史、公开热度、发行日期及不感兴趣设置。");
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 46
        // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
        // <<< MAIN-DEFENSE
        return success({{"result", QJsonObject{{"items", rows}, {"message", recommended.value->message + note},
            {"fallback", recommended.value->fallback}, {"engine", "temporal"},
            {"generated_at_utc", QString::number(recommended.value->generatedAtUtc)},
            {"contains_synthetic_data", recommended.value->containsSyntheticData}}}});
    }
    // >>> MAIN-DEFENSE | dispatch｜处理阶段 47
    // 请求分支：get_trending。当前session提供身份；先验证参数和操作权限，再执行该分支。
    // <<< MAIN-DEFENSE
    if (type == "get_trending") {
        care_contract::TrendingRequest input;
        if (!parseTrendingRequest(payload, input).ok()) return invalid();
        const auto result = currentTrending(input);
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 48
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!result.status.ok() || !result.value) return dbError();
        return success({{"result", trendingJson(*result.value)}});
    }
    if (type == "get_footprint") {
        care_contract::FootprintRequest input;
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 49
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!readRange(payload.value("range"), input.range)) return invalid();
        const auto comparison = payload.value("comparison");
        if (!comparison.isUndefined() && !comparison.isNull()) {
            care_contract::TimeRange range;
            // >>> MAIN-DEFENSE | dispatch｜处理阶段 50
            // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
            // <<< MAIN-DEFENSE
            if (!readRange(comparison, range)) return invalid();
            input.comparison = range;
        }
        const auto valid = care_contract::validate(input);
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 51
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!valid.ok()) return fail("InvalidArgument", valid.message);
        const auto history = queryBehavior(session.userId, {0, care_contract::MaxUtcExclusive});
        if (!history) return dbError();
        care_algorithms::PersonalSnapshot snapshot;
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 52
        // 使用当前连接的会话身份检查或构造业务上下文；客户端传来的编号不能替代登录身份。
        // <<< MAIN-DEFENSE
        snapshot.userKey = QString::number(session.userId);
        snapshot.nowUtc = QDateTime::currentSecsSinceEpoch();
        for (const auto& item : *history) {
            care_algorithms::ObservedBehavior event;
            event.userKey = snapshot.userKey;
            event.record = item.record;
            event.publicEligible = item.publicEligible;
            // >>> MAIN-DEFENSE | dispatch｜处理阶段 53
            // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
            // <<< MAIN-DEFENSE
            if (item.record.event.resource && item.mediaSnapshot) {
                care_contract::ResourceSummary work;
                work.resource = *item.record.event.resource;
                work.media = *item.mediaSnapshot;
                work.people = item.peopleSnapshot;
                event.workSnapshot = work;
            }
            snapshot.events.append(event);
        }
        const auto report = care_algorithms::buildFootprint(input, snapshot);
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 54
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!report.status.ok() || !report.value)
            return fail(report.status.code == ErrorCode::InvalidArgument ? "InvalidArgument" : "ServerError", report.status.message);
        QJsonObject output{{"current", periodJson(report.value->current)}, {"generated_at_utc", QString::number(report.value->generatedAtUtc)},
            {"message", report.value->message + QStringLiteral(" 未确认的人物关联不纳入人物统计。")},
            {"contains_synthetic_data", report.value->containsSyntheticData}};
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 55
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (report.value->comparison) output.insert("comparison", periodJson(*report.value->comparison));
        return success({{"report", output}});
    }
    if (type == "get_resource") {
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 56
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!payload.value("source").isObject()) return invalid();
        const auto source = payload.value("source").toObject();
        const qint64 warehouseId = readId(source.value("warehouse_id"));
        const qint64 itemId = readId(source.value("item_id"));
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 57
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!warehouseId || !itemId) return invalid();
        query.prepare("SELECT w.owner_id,w.is_public,i.media FROM warehouses w "
                      "JOIN warehouse_items i ON i.warehouse_id=w.id WHERE w.id=? AND i.id=?");
        query.addBindValue(warehouseId); query.addBindValue(itemId);
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 58
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!query.exec()) return dbError();
        if (!query.next()) return fail("NotFound", QStringLiteral("资源不存在。"));
        if (query.value(0).toLongLong() != session.userId && !query.value(1).toBool())
            return fail("PermissionDenied", QStringLiteral("没有此资源的访问权限。"));
        const auto media = QJsonDocument::fromJson(query.value(2).toByteArray()).object();
        MediaRecord decoded; QString error;
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 59
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!media_json::fromJson(media, decoded, error)) return dbError();
        query.finish();
        query.prepare("INSERT OR IGNORE INTO work_sources(warehouse_id,item_id,work_id) VALUES(?,?,?)");
        query.addBindValue(warehouseId); query.addBindValue(itemId);
        query.addBindValue(QStringLiteral("srv:") + QUuid::createUuid().toString(QUuid::WithoutBraces));
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 60
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!query.exec()) return dbError();
        query.prepare("SELECT work_id FROM work_sources WHERE warehouse_id=? AND item_id=?");
        query.addBindValue(warehouseId); query.addBindValue(itemId);
        if (!query.exec() || !query.next()) return dbError();
        const care_contract::ResourceSummary work{{query.value(0).toString(), {warehouseId, itemId}}, decoded, {}};
        query.finish();
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 61
        // 使用当前连接的会话身份检查或构造业务上下文；客户端传来的编号不能替代登录身份。
        // <<< MAIN-DEFENSE
        const auto catalog = personCatalog(session.userId, {work});
        if (!catalog.status.ok() || !catalog.value) return dbError();
        return success({{"work", workJson(catalog.value->readableWorks.first())}});
    }
    // >>> MAIN-DEFENSE | dispatch｜处理阶段 62
    // 请求分支：get_participation_settings。当前session提供身份；先验证参数和操作权限，再执行该分支。
    // <<< MAIN-DEFENSE
    if (type == "get_participation_settings") {
        query.prepare("SELECT history_enabled,personalization_enabled,contribute_public_trends FROM behavior_settings WHERE user_id=?");
        query.addBindValue(session.userId);
        if (!query.exec()) return dbError();
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 63
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!query.next()) return success({{"settings", QJsonObject{{"history_enabled", true}, {"personalization_enabled", true}, {"contribute_public_trends", false}}}});
        return success({{"settings", QJsonObject{{"history_enabled", query.value(0).toBool()},
                         {"personalization_enabled", query.value(1).toBool()}, {"contribute_public_trends", query.value(2).toBool()}}}});
    }
    // >>> MAIN-DEFENSE | dispatch｜处理阶段 64
    // 请求分支：set_participation_settings。当前session提供身份；先验证参数和操作权限，再执行该分支。
    // <<< MAIN-DEFENSE
    if (type == "set_participation_settings") {
        if (!payload.value("settings").isObject()) return invalid();
        const auto settings = payload.value("settings").toObject();
        if (!settings.value("history_enabled").isBool() || !settings.value("personalization_enabled").isBool() ||
            !settings.value("contribute_public_trends").isBool()) return invalid();
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 65
        // 开始数据库事务；下方相关写入必须一起成功，失败路径执行回滚。
        // <<< MAIN-DEFENSE
        if (!db_.transaction()) return dbError();
        query.prepare("INSERT INTO behavior_settings VALUES(?,?,?,?) ON CONFLICT(user_id) DO UPDATE SET "
                      "history_enabled=excluded.history_enabled,personalization_enabled=excluded.personalization_enabled,"
                      "contribute_public_trends=excluded.contribute_public_trends");
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 66
        // 使用当前连接的会话身份检查或构造业务上下文；客户端传来的编号不能替代登录身份。
        // <<< MAIN-DEFENSE
        query.addBindValue(session.userId);
        query.addBindValue(settings.value("history_enabled").toBool());
        query.addBindValue(settings.value("personalization_enabled").toBool());
        query.addBindValue(settings.value("contribute_public_trends").toBool());
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 67
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!query.exec()) { db_.rollback(); return dbError(); }
        if (!settings.value("contribute_public_trends").toBool()) {
            query.prepare("UPDATE behavior_events SET public_eligible=0 WHERE user_id=?");
            query.addBindValue(session.userId);
            // >>> MAIN-DEFENSE | dispatch｜处理阶段 68
            // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
            // <<< MAIN-DEFENSE
            if (!query.exec()) { db_.rollback(); return dbError(); }
        }
        if (!db_.commit()) { db_.rollback(); return dbError(); }
        return success();
    }
    // >>> MAIN-DEFENSE | dispatch｜处理阶段 69
    // 请求分支：clear_history。当前session提供身份；先验证参数和操作权限，再执行该分支。
    // <<< MAIN-DEFENSE
    if (type == "clear_history") {
        query.prepare("DELETE FROM behavior_events WHERE user_id=?");
        query.addBindValue(session.userId);
        if (!query.exec()) return dbError();
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 70
        // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
        // <<< MAIN-DEFENSE
        return success();
    }
    if (type == "record_behavior") {
        if (!payload.value("event").isObject()) return invalid();
        const auto event = payload.value("event").toObject();
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 71
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!event.value("event_id").isString() || !event.value("action").isString()) return invalid();
        care_contract::BehaviorEvent value;
        value.eventId = event.value("event_id").toString();
        const auto action = event.value("action").toString();
        QByteArray snapshot;
        bool sourcePublic = true;
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 72
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (action == "search") {
            if (!event.value("query").isString() || (!event.value("resource").isUndefined() && !event.value("resource").isNull())) return invalid();
            value.action = care_contract::BehaviorAction::Search;
            value.query = event.value("query").toString();
        } else if (action == "view") {
            // >>> MAIN-DEFENSE | dispatch｜处理阶段 73
            // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
            // <<< MAIN-DEFENSE
            if (!event.value("resource").isObject() || (!event.value("query").isUndefined() && !event.value("query").isString())) return invalid();
            const auto resource = event.value("resource").toObject();
            if (!resource.value("work_id").isString() || !resource.value("source").isObject()) return invalid();
            const auto source = resource.value("source").toObject();
            value.action = care_contract::BehaviorAction::View;
            value.query = event.value("query").isUndefined() ? QStringLiteral("") : event.value("query").toString();
            value.resource = care_contract::ResourceRef{resource.value("work_id").toString(),
                {readId(source.value("warehouse_id")), readId(source.value("item_id"))}};
        } else return invalid();
        const auto status = care_contract::validate(value);
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 74
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!status.ok()) return fail("InvalidArgument", status.message);
        // Check the authenticated user's existing event before looking up a resource:
        // retries must remain idempotent even if that resource was later deleted.
        query.prepare("SELECT action,query,work_id,warehouse_id,item_id FROM behavior_events WHERE user_id=? AND event_id=?");
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 75
        // 使用当前连接的会话身份检查或构造业务上下文；客户端传来的编号不能替代登录身份。
        // <<< MAIN-DEFENSE
        query.addBindValue(session.userId);
        query.addBindValue(value.eventId);
        if (!query.exec()) return dbError();
        if (query.next()) {
            // >>> MAIN-DEFENSE | dispatch｜处理阶段 76
            // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
            // <<< MAIN-DEFENSE
            if (query.value(0).toString() != action || query.value(1).toString() != value.query ||
                (value.resource && (query.value(2).toString() != value.resource->workId ||
                  query.value(3).toLongLong() != value.resource->source.warehouseId || query.value(4).toLongLong() != value.resource->source.itemId)))
                return fail("InvalidArgument", QStringLiteral("行为编号已用于其他内容。"));
            // >>> MAIN-DEFENSE | dispatch｜处理阶段 77
            // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
            // <<< MAIN-DEFENSE
            return success();
        }
        if (query.lastError().isValid()) return dbError();
        query.finish();
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 78
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (value.resource) {
            query.prepare("SELECT w.owner_id,w.is_public,i.media,s.work_id FROM warehouses w "
                          "JOIN warehouse_items i ON i.warehouse_id=w.id "
                          "LEFT JOIN work_sources s ON s.warehouse_id=w.id AND s.item_id=i.id WHERE w.id=? AND i.id=?");
            query.addBindValue(value.resource->source.warehouseId); query.addBindValue(value.resource->source.itemId);
            // >>> MAIN-DEFENSE | dispatch｜处理阶段 79
            // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
            // <<< MAIN-DEFENSE
            if (!query.exec()) return dbError();
            if (!query.next()) return fail("NotFound", QStringLiteral("资源不存在。"));
            if (query.value(0).toLongLong() != session.userId && !query.value(1).toBool())
                return fail("PermissionDenied", QStringLiteral("没有此资源的访问权限。"));
            // >>> MAIN-DEFENSE | dispatch｜处理阶段 80
            // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
            // <<< MAIN-DEFENSE
            if (query.value(3).toString() != value.resource->workId) return invalid();
            sourcePublic = query.value(1).toBool(); snapshot = query.value(2).toByteArray();
            query.finish();
        }
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 81
        // 准备参数化SQL，涉及 behavior_settings。随后把实际参数绑定到问号，执行并检查错误；prepare本身尚未完成读写。
        // <<< MAIN-DEFENSE
        query.prepare("SELECT history_enabled,contribute_public_trends FROM behavior_settings WHERE user_id=?");
        query.addBindValue(session.userId);
        if (!query.exec()) return dbError();
        const bool hasSettings = query.next();
        const bool historyEnabled = !hasSettings || query.value(0).toBool();
        const bool publicEligible = hasSettings && query.value(1).toBool() && sourcePublic;
        query.finish();
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 82
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!historyEnabled) return success();
        if (value.resource) {
            auto object = QJsonDocument::fromJson(snapshot).object();
            MediaRecord media; QString error;
            // >>> MAIN-DEFENSE | dispatch｜处理阶段 83
            // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
            // <<< MAIN-DEFENSE
            if (!media_json::fromJson(object, media, error)) return dbError();
            const auto catalog = personCatalog(session.userId, {{*value.resource, media, {}}});
            if (!catalog.status.ok() || !catalog.value) return dbError();
            object.insert(person_snapshot_json::Field,
                person_snapshot_json::encode(catalog.value->readableWorks.first().people));
            snapshot = QJsonDocument(object).toJson(QJsonDocument::Compact);
        }
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 84
        // 准备参数化SQL，涉及 behavior_events。随后把实际参数绑定到问号，执行并检查错误；prepare本身尚未完成读写。
        // <<< MAIN-DEFENSE
        query.prepare("INSERT INTO behavior_events(user_id,event_id,action,query,occurred_at_utc,public_eligible,work_id,warehouse_id,item_id,media_snapshot) VALUES(?,?,?,?,?,?,?,?,?,?) ON CONFLICT(user_id,event_id) DO NOTHING");
        query.addBindValue(session.userId);
        query.addBindValue(value.eventId);
        query.addBindValue(action);
        query.addBindValue(value.query);
        query.addBindValue(QDateTime::currentSecsSinceEpoch());
        query.addBindValue(publicEligible);
        query.addBindValue(value.resource ? QVariant(value.resource->workId) : QVariant());
        query.addBindValue(value.resource ? QVariant(value.resource->source.warehouseId) : QVariant());
        query.addBindValue(value.resource ? QVariant(value.resource->source.itemId) : QVariant());
        query.addBindValue(value.resource ? QVariant(snapshot) : QVariant());
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 85
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!query.exec()) return dbError();
        if (query.numRowsAffected() == 0) {
            query.prepare("SELECT action,query,work_id,warehouse_id,item_id FROM behavior_events WHERE user_id=? AND event_id=?");
            query.addBindValue(session.userId); query.addBindValue(value.eventId);
            // >>> MAIN-DEFENSE | dispatch｜处理阶段 86
            // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
            // <<< MAIN-DEFENSE
            if (!query.exec() || !query.next()) return dbError();
            if (query.value(0).toString() != action || query.value(1).toString() != value.query ||
                (value.resource && (query.value(2).toString() != value.resource->workId ||
                 query.value(3).toLongLong() != value.resource->source.warehouseId || query.value(4).toLongLong() != value.resource->source.itemId)))
                // >>> MAIN-DEFENSE | dispatch｜处理阶段 87
                // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
                // <<< MAIN-DEFENSE
                return fail("InvalidArgument", QStringLiteral("行为编号已用于其他内容。"));
        }
        return success();
    }
    // >>> MAIN-DEFENSE | dispatch｜处理阶段 88
    // 请求分支：list_my_warehouses。当前session提供身份；先验证参数和操作权限，再执行该分支。
    // <<< MAIN-DEFENSE
    if (type == "list_my_warehouses" || type == "list_public_warehouses") {
        if (type == "list_my_warehouses") {
            query.prepare("SELECT id,name,owner_id,is_public FROM warehouses WHERE owner_id=? ORDER BY id");
            query.addBindValue(session.userId);
        } else {
            // >>> MAIN-DEFENSE | dispatch｜处理阶段 89
            // 准备参数化SQL，涉及 warehouses。随后把实际参数绑定到问号，执行并检查错误；prepare本身尚未完成读写。
            // <<< MAIN-DEFENSE
            query.prepare("SELECT id,name,owner_id,is_public FROM warehouses WHERE is_public=1 ORDER BY id");
        }
        if (!query.exec())
            return dbError();
        QJsonArray list;
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 90
        // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
        // <<< MAIN-DEFENSE
        while (query.next())
            list.append(summaryJson(readSummary(query)));
        return success({{"warehouses", list}});
    }
    // >>> MAIN-DEFENSE | dispatch｜处理阶段 91
    // 请求分支：create_warehouse。当前session提供身份；先验证参数和操作权限，再执行该分支。
    // <<< MAIN-DEFENSE
    if (type == "create_warehouse") {
        const QString name = payload.value("name").toString().trimmed();
        const QString visibility = payload.value("visibility").toString();
        if (name.isEmpty() || name.size() > 100 || (visibility != "public" && visibility != "private"))
            // >>> MAIN-DEFENSE | dispatch｜处理阶段 92
            // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
            // <<< MAIN-DEFENSE
            return invalid();
        const bool publicAccess = visibility == "public";
        query.prepare("INSERT INTO warehouses(name,owner_id,is_public) VALUES (?,?,?)");
        query.addBindValue(name);
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 93
        // 使用当前连接的会话身份检查或构造业务上下文；客户端传来的编号不能替代登录身份。
        // <<< MAIN-DEFENSE
        query.addBindValue(session.userId);
        query.addBindValue(publicAccess);
        if (!query.exec())
            return dbError();
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 94
        // 使用当前连接的会话身份检查或构造业务上下文；客户端传来的编号不能替代登录身份。
        // <<< MAIN-DEFENSE
        return success({{"warehouse", summaryJson({query.lastInsertId().toLongLong(), name, session.userId,
                                            publicAccess ? Visibility::Public : Visibility::Private})}});
    }

    // >>> MAIN-DEFENSE | dispatch｜处理阶段 95
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (type != "get_warehouse" && type != "add_item" && type != "remove_item")
        return fail("InvalidArgument", QStringLiteral("未知请求类型。"));
    const qint64 warehouseId = readId(payload.value("warehouse_id"));
    if (!warehouseId)
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 96
        // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
        // <<< MAIN-DEFENSE
        return invalid();
    query.prepare("SELECT id,name,owner_id,is_public FROM warehouses WHERE id=?");
    query.addBindValue(warehouseId);
    if (!query.exec())
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 97
        // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
        // <<< MAIN-DEFENSE
        return dbError();
    if (!query.next())
        return fail("NotFound", QStringLiteral("仓库不存在。"));
    const auto warehouse = readSummary(query);
    query.finish();
    query.prepare("SELECT is_admin FROM network_user_access WHERE user_id=? AND disabled=0");
    query.addBindValue(session.userId);
    if (!query.exec()) return dbError();
    const bool administrator = query.next() && query.value(0).toBool();
    query.finish();
    if (warehouse.ownerId != session.userId && warehouse.visibility != Visibility::Public)
        return fail("PermissionDenied", QStringLiteral("没有此仓库的操作权限。"));
    if (warehouse.ownerId != session.userId && type != "get_warehouse" && !administrator)
    // >>> MAIN-DEFENSE | dispatch｜处理阶段 98
    // 使用当前连接的会话身份检查或构造业务上下文；客户端传来的编号不能替代登录身份。
    // <<< MAIN-DEFENSE
        return fail("PermissionDenied", QStringLiteral("没有此仓库的操作权限。"));

    if (type == "get_warehouse") {
        const auto items = listItems(db_, warehouseId);
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 99
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!items)
            return dbError();
        QJsonArray list;
        for (const auto& item : *items)
            list.append(itemJson(item));
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 100
        // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
        // <<< MAIN-DEFENSE
        return success({{"warehouse", summaryJson(warehouse)}, {"items", list}});
    }
    if (type == "add_item") {
        MediaRecord media;
        QString error;
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 101
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!media_json::fromJson(payload.value("media").toObject(), media, error))
            return fail("InvalidArgument", error);
        const auto snapshot = QJsonDocument(media_json::toJson(media)).toJson(QJsonDocument::Compact);
        if (snapshot.size() > 64 * 1024)
            // >>> MAIN-DEFENSE | dispatch｜处理阶段 102
            // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
            // <<< MAIN-DEFENSE
            return invalid();
        if (!db_.transaction()) return dbError();
        query.prepare("INSERT INTO warehouse_items(warehouse_id,media) VALUES (?,?)");
        query.addBindValue(warehouseId);
        query.addBindValue(snapshot);
        if (!query.exec()) { db_.rollback(); return dbError(); }
        const auto insertedId = query.lastInsertId().toLongLong();
        if (administrator) {
            query.prepare("INSERT INTO admin_audit(actor_id,target_id,action,occurred_at_utc) VALUES (?,?,?,?)");
            query.addBindValue(session.userId); query.addBindValue(insertedId);
            query.addBindValue("add_item"); query.addBindValue(QDateTime::currentSecsSinceEpoch());
            if (!query.exec()) { db_.rollback(); return dbError(); }
        }
        if (!db_.commit()) { db_.rollback(); return dbError(); }
        return success({{"item", itemJson({insertedId, media})}});
        // >>> MAIN-DEFENSE | dispatch｜处理阶段 103
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
    }
    const qint64 itemId = readId(payload.value("item_id"));
    // >>> MAIN-DEFENSE | dispatch｜处理阶段 104
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!itemId)
        return invalid();
    if (!db_.transaction()) return dbError();
    query.prepare("DELETE FROM warehouse_items WHERE id=? AND warehouse_id=?");
    query.addBindValue(itemId);
    query.addBindValue(warehouseId);
    if (!query.exec()) { db_.rollback(); return dbError(); }
    if (query.numRowsAffected() != 1) {
        db_.rollback();
    // >>> MAIN-DEFENSE | dispatch｜处理阶段 105
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
        return fail("NotFound", QStringLiteral("条目不存在。"));
    }
    if (administrator) {
        query.prepare("INSERT INTO admin_audit(actor_id,target_id,action,occurred_at_utc) VALUES (?,?,?,?)");
        query.addBindValue(session.userId); query.addBindValue(itemId);
        query.addBindValue("remove_item"); query.addBindValue(QDateTime::currentSecsSinceEpoch());
        if (!query.exec()) { db_.rollback(); return dbError(); }
    }
    if (!db_.commit()) { db_.rollback(); return dbError(); }
    // >>> MAIN-DEFENSE | dispatch｜处理阶段 106
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return success();
}
