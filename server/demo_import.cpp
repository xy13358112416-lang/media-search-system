// >>> MAIN-DEFENSE | 主逻辑文件导读
// 演示导入CLI：只创建或重跑匹配的独立演示库，导入合成用户/作品/历史，密码用哈希保存，不能覆盖未标记的正式库。
// 阅读时先看函数入口说明，再按“处理阶段”往下；到每个return核对成功、空结果和错误是否区别。
// <<< MAIN-DEFENSE
#include "warehouse_server.h"
#include "media_json.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTextStream>
#include <QUuid>
#include <sodium.h>

namespace {
struct Work { QString id; QByteArray media; };
struct Event { QString user; QString id; QString action; QString work; QString query; qint64 time = 0; };
struct Fixture { QList<QString> users; QList<Work> works; QList<Event> events; QByteArray digest; };
struct Source { qint64 warehouse = 0; qint64 item = 0; QString work; QByteArray media; };

// >>> MAIN-DEFENSE | 函数入口｜decimal
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：bool decimal(const QJsonValue& value, qint64& number) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool decimal(const QJsonValue& value, qint64& number) {
    if (!value.isString()) return false;
    static const QRegularExpression re(QStringLiteral("\\A(?:0|[1-9][0-9]*)\\z"));
    const QString text = value.toString(); bool ok = false;
    number = text.toLongLong(&ok);
    // >>> MAIN-DEFENSE | decimal｜处理阶段 01
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return ok && re.match(text).hasMatch();
}
// >>> MAIN-DEFENSE | 函数入口｜loadFixture
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：bool loadFixture(const QString& path, Fixture& output, QString& error) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool loadFixture(const QString& path, Fixture& output, QString& error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024 * 1024) { error = QStringLiteral("无法读取模拟数据文件，或文件过大。"); return false; }
    const QByteArray bytes = file.readAll();
    QJsonParseError parse; const auto doc = QJsonDocument::fromJson(bytes, &parse);
    // >>> MAIN-DEFENSE | loadFixture｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) { error = QStringLiteral("模拟数据 JSON 无效。"); return false; }
    const auto root = doc.object();
    if (root.value("schema_version").toInt() != 1 || !root.value("synthetic").isBool() || !root.value("synthetic").toBool() ||
        !root.value("users").isArray() || !root.value("works").isArray() || !root.value("events").isArray()) {
        error = QStringLiteral("模拟数据结构或标记无效。"); return false;
    }
    const auto users = root.value("users").toArray(), works = root.value("works").toArray(), events = root.value("events").toArray();
    // >>> MAIN-DEFENSE | loadFixture｜处理阶段 02
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (users.isEmpty() || users.size() > 200 || works.isEmpty() || works.size() > 5000 || events.size() > 100000) { error = QStringLiteral("模拟数据数量无效。"); return false; }
    static const QRegularExpression userFormat(QStringLiteral("\\Asim_user_(?:0[1-9]|[1-9][0-9]|1[0-9]{2}|200)\\z"));
    QSet<QString> userIds, workIds, eventIds;
    for (const auto& value : users) {
        const auto o = value.toObject(); const QString id = o.value("user_ref").toString();
        // >>> MAIN-DEFENSE | loadFixture｜处理阶段 03
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!value.isObject() || !userFormat.match(id).hasMatch() || userIds.contains(id)) { error = QStringLiteral("模拟用户编号无效。"); return false; }
        userIds.insert(id); output.users.append(id);
    }
    for (const auto& value : works) {
        // >>> MAIN-DEFENSE | loadFixture｜处理阶段 04
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!value.isObject()) { error = QStringLiteral("模拟作品无效。"); return false; }
        auto o = value.toObject(); const QString id = o.value("id").toString();
        if (id.isEmpty() || o.value("work_id").toString() != id || workIds.contains(id) ||
            !care_contract::validate(care_contract::ResourceRef{id, {1, 1}}).ok()) { error = QStringLiteral("模拟作品编号无效。"); return false; }
        // >>> MAIN-DEFENSE | loadFixture｜处理阶段 05
        // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
        // <<< MAIN-DEFENSE
        for (const char* field : {"actors", "awards"}) {
            if (!o.value(field).isString()) { error = QStringLiteral("模拟媒体列表无效。"); return false; }
            QJsonParseError e; const auto list = QJsonDocument::fromJson(o.value(field).toString().toUtf8(), &e);
            if (e.error != QJsonParseError::NoError || !list.isArray()) { error = QStringLiteral("模拟媒体列表无效。"); return false; }
            o.insert(field, list.array());
        }
        MediaRecord record; QString mediaError;
        // >>> MAIN-DEFENSE | loadFixture｜处理阶段 06
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!media_json::fromJson(o, record, mediaError)) { error = QStringLiteral("模拟媒体格式无效：") + mediaError; return false; }
        workIds.insert(id); output.works.append({id, QJsonDocument(media_json::toJson(record)).toJson(QJsonDocument::Compact)});
    }
    qint64 start = 0, end = 0;
    // >>> MAIN-DEFENSE | loadFixture｜处理阶段 07
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!decimal(root.value("start_utc"), start) || !decimal(root.value("now_utc"), end) || start >= end || end > care_contract::MaxUtcExclusive) {
        error = QStringLiteral("模拟时间范围无效。"); return false;
    }
    for (const auto& value : events) {
        const auto o = value.toObject(); Event event;
        event.user = o.value("user_ref").toString(); event.id = o.value("event_id").toString();
        event.action = o.value("action").toString(); event.work = o.value("work_id").toString(); event.query = o.value("query").toString();
        care_contract::BehaviorEvent check; check.eventId = event.id;
        // >>> MAIN-DEFENSE | loadFixture｜处理阶段 08
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (event.action == "view") { check.action = care_contract::BehaviorAction::View; check.query = event.query; check.resource = care_contract::ResourceRef{event.work, {1, 1}}; }
        else if (event.action == "search") { check.action = care_contract::BehaviorAction::Search; check.query = event.query; if (!event.work.isEmpty()) { error = QStringLiteral("搜索事件不能包含作品。"); return false; } }
        else { error = QStringLiteral("模拟事件动作无效。"); return false; }
        const QString unique = event.user + QChar(0x1f) + event.id;
        // >>> MAIN-DEFENSE | loadFixture｜处理阶段 09
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!value.isObject() || !userIds.contains(event.user) || eventIds.contains(unique) || !care_contract::validate(check).ok() ||
            (event.action == "view" && !workIds.contains(event.work)) || !decimal(o.value("occurred_at_utc"), event.time) ||
            event.time < start || event.time >= end) { error = QStringLiteral("模拟事件数据或引用无效。"); return false; }
        eventIds.insert(unique); output.events.append(event);
    }
    output.digest = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
    // >>> MAIN-DEFENSE | loadFixture｜处理阶段 10
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return true;
}

// >>> MAIN-DEFENSE | 函数入口｜existingDemoMatches
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：bool existingDemoMatches(const QString& path, const Fixture& fixture, QString& error) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool existingDemoMatches(const QString& path, const Fixture& fixture, QString& error) {
    const QString connection = QUuid::createUuid().toString(); bool ok = false;
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
        db.setConnectOptions("QSQLITE_OPEN_READONLY"); db.setDatabaseName(path);
        // >>> MAIN-DEFENSE | existingDemoMatches｜处理阶段 01
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (db.open()) {
            QSqlQuery q(db);
            if (q.exec("SELECT fixture_hash,expected_users,expected_works,expected_events FROM demo_import_metadata WHERE id=1 AND kind='media-demo-v1'") && q.next() &&
                q.value(0).toByteArray() == fixture.digest && q.value(1).toInt() == fixture.users.size() &&
                q.value(2).toInt() == fixture.works.size() && q.value(3).toInt() == fixture.events.size()) {
                q.finish();
                // >>> MAIN-DEFENSE | existingDemoMatches｜处理阶段 02
                // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
                // <<< MAIN-DEFENSE
                if (q.exec("SELECT (SELECT COUNT(*) FROM demo_user_map), (SELECT COUNT(*) FROM demo_media_map), "
                           "(SELECT COUNT(*) FROM behavior_events), (SELECT COUNT(*) FROM behavior_events WHERE synthetic<>1), "
                           "(SELECT COUNT(*) FROM behavior_events e LEFT JOIN work_sources w ON w.warehouse_id=e.warehouse_id AND w.item_id=e.item_id AND w.work_id=e.work_id WHERE e.action='view' AND w.work_id IS NULL)") && q.next())
                    ok = q.value(0).toInt() == fixture.users.size() && q.value(1).toInt() == fixture.works.size() &&
                         q.value(2).toInt() == fixture.events.size() && q.value(3).toInt() == 0 && q.value(4).toInt() == 0;
                // >>> MAIN-DEFENSE | existingDemoMatches｜处理阶段 03
                // 准备本步骤的内存查找、去重或汇总容器；它们不是数据库表，生命周期通常只在本次调用内。
                // <<< MAIN-DEFENSE
                QHash<QString, qint64> users; QHash<QString, Source> sources;
                if (ok) {
                    q.finish(); ok = q.exec("SELECT m.user_ref,m.user_id FROM demo_user_map m JOIN network_users u ON u.id=m.user_id");
                    while (ok && q.next()) users.insert(q.value(0).toString(), q.value(1).toLongLong());
                    // >>> MAIN-DEFENSE | existingDemoMatches｜处理阶段 04
                    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
                    // <<< MAIN-DEFENSE
                    if (q.lastError().isValid() || users.size() != fixture.users.size()) ok = false;
                    for (const auto& user : fixture.users) if (!users.contains(user)) ok = false;
                }
                if (ok) {
                    q.finish(); ok = q.exec("SELECT m.media_id,m.warehouse_id,m.item_id,m.work_id,i.media FROM demo_media_map m "
                        "JOIN warehouse_items i ON i.id=m.item_id AND i.warehouse_id=m.warehouse_id "
                        "JOIN warehouses w ON w.id=m.warehouse_id AND w.is_public=1 "
                        "JOIN work_sources s ON s.warehouse_id=m.warehouse_id AND s.item_id=m.item_id AND s.work_id=m.work_id");
                    // >>> MAIN-DEFENSE | existingDemoMatches｜处理阶段 05
                    // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
                    // <<< MAIN-DEFENSE
                    while (ok && q.next()) sources.insert(q.value(0).toString(), {q.value(1).toLongLong(), q.value(2).toLongLong(), q.value(3).toString(), q.value(4).toByteArray()});
                    if (q.lastError().isValid() || sources.size() != fixture.works.size()) ok = false;
                    for (const auto& work : fixture.works)
                        if (!sources.contains(work.id) || sources.value(work.id).media != work.media) ok = false;
                }
                // >>> MAIN-DEFENSE | existingDemoMatches｜处理阶段 06
                // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
                // <<< MAIN-DEFENSE
                if (ok) {
                    q.finish();
                    q.prepare("SELECT action,query,occurred_at_utc,synthetic,work_id,warehouse_id,item_id,media_snapshot FROM behavior_events WHERE user_id=? AND event_id=?");
                    for (const auto& event : fixture.events) {
                        q.bindValue(0, users.value(event.user)); q.bindValue(1, event.id);
                        // >>> MAIN-DEFENSE | existingDemoMatches｜处理阶段 07
                        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
                        // <<< MAIN-DEFENSE
                        if (!q.exec() || !q.next()) { ok = false; break; }
                        const bool view = event.action == "view"; const auto source = sources.value(event.work);
                        if (q.value(0).toString() != event.action || q.value(1).toString() != event.query ||
                            q.value(2).toLongLong() != event.time || !q.value(3).toBool() ||
                            (view && (q.value(4).toString() != source.work || q.value(5).toLongLong() != source.warehouse ||
                                      q.value(6).toLongLong() != source.item || q.value(7).toByteArray() != source.media)) ||
                            (!view && (!q.value(4).isNull() || !q.value(5).isNull() || !q.value(6).isNull() || !q.value(7).isNull()))) {
                            ok = false; break;
                        }
                        q.finish();
                    }
                }
            }
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(connection);
    // >>> MAIN-DEFENSE | existingDemoMatches｜处理阶段 08
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!ok) error = QStringLiteral("目标不是完整且匹配的演示库；未写入已有文件。");
    return ok;
}

// >>> MAIN-DEFENSE | 函数入口｜importNew
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：bool importNew(const QString& path, const Fixture& fixture, const QByteArray& password, QString& error) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool importNew(const QString& path, const Fixture& fixture, const QByteArray& password, QString& error) {
    QFile reserved(path);
    if (!reserved.open(QIODevice::WriteOnly | QIODevice::NewOnly)) { error = QStringLiteral("无法创建新的演示数据库。"); return false; }
    reserved.close();
    WarehouseServer server(llm_recommendation::Config{});
    // >>> MAIN-DEFENSE | importNew｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!server.openDatabase(path, error)) return false;
    const QString connection = QUuid::createUuid().toString(); bool ok = false;
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", connection); db.setDatabaseName(path);
        // >>> MAIN-DEFENSE | importNew｜处理阶段 02
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (db.open()) {
            QSqlQuery q(db);
            if (q.exec("PRAGMA foreign_keys=ON") && db.transaction()) {
                auto run = [&q](const char* sql) { return q.exec(QString::fromLatin1(sql)); };
                ok = run("CREATE TABLE demo_import_metadata(id INTEGER PRIMARY KEY CHECK(id=1),kind TEXT NOT NULL,fixture_hash TEXT NOT NULL,expected_users INTEGER NOT NULL,expected_works INTEGER NOT NULL,expected_events INTEGER NOT NULL)") &&
                     run("CREATE TABLE demo_user_map(user_ref TEXT PRIMARY KEY,user_id INTEGER NOT NULL UNIQUE REFERENCES network_users(id))") &&
                     run("CREATE TABLE demo_media_map(media_id TEXT PRIMARY KEY,warehouse_id INTEGER NOT NULL,item_id INTEGER NOT NULL,work_id TEXT NOT NULL UNIQUE)");
                // >>> MAIN-DEFENSE | importNew｜处理阶段 03
                // 准备本步骤的内存查找、去重或汇总容器；它们不是数据库表，生命周期通常只在本次调用内。
                // <<< MAIN-DEFENSE
                QHash<QString, qint64> users; QHash<QString, Source> sources;
                for (const auto& user : fixture.users) {
                    if (!ok) break;
                    char hash[crypto_pwhash_STRBYTES];
                    // >>> MAIN-DEFENSE | importNew｜处理阶段 04
                    // 生成密码哈希：明文只用于本次计算，保存带盐哈希；这不是可逆加密。
                    // <<< MAIN-DEFENSE
                    if (crypto_pwhash_str(hash, password.constData(), password.size(), crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0) { ok = false; break; }
                    q.prepare("INSERT INTO network_users(username,username_key,password_hash) VALUES(?,?,?)");
                    q.addBindValue(user); q.addBindValue(user); q.addBindValue(QString::fromLatin1(hash));
                    ok = q.exec(); if (!ok) break;
                    const qint64 id = q.lastInsertId().toLongLong(); users.insert(user, id);
                    // >>> MAIN-DEFENSE | importNew｜处理阶段 05
                    // 准备参数化SQL，涉及 demo_user_map、behavior_settings、warehouses。随后把实际参数绑定到问号，执行并检查错误；prepare本身尚未完成读写。
                    // <<< MAIN-DEFENSE
                    q.prepare("INSERT INTO demo_user_map VALUES(?,?)"); q.addBindValue(user); q.addBindValue(id); ok = q.exec();
                    if (!ok) break;
                    q.prepare("INSERT INTO behavior_settings VALUES(?,1,1,1)"); q.addBindValue(id); ok = q.exec();
                }
                qint64 warehouseId = 0;
                // >>> MAIN-DEFENSE | importNew｜处理阶段 06
                // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
                // <<< MAIN-DEFENSE
                if (ok) {
                    q.prepare("INSERT INTO warehouses(name,owner_id,is_public) VALUES(?,?,1)");
                    q.addBindValue(QStringLiteral("模拟媒体库")); q.addBindValue(users.value(fixture.users.first())); ok = q.exec();
                    if (ok) warehouseId = q.lastInsertId().toLongLong();
                }
                // >>> MAIN-DEFENSE | importNew｜处理阶段 07
                // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
                // <<< MAIN-DEFENSE
                for (const auto& work : fixture.works) {
                    if (!ok) break;
                    q.prepare("INSERT INTO warehouse_items(warehouse_id,media) VALUES(?,?)"); q.addBindValue(warehouseId); q.addBindValue(work.media); ok = q.exec();
                    if (!ok) break;
                    const qint64 itemId = q.lastInsertId().toLongLong();
                    const QString workId = QStringLiteral("srv:") + QUuid::createUuid().toString(QUuid::WithoutBraces);
                    // >>> MAIN-DEFENSE | importNew｜处理阶段 08
                    // 准备参数化SQL，涉及 work_sources、demo_media_map。随后把实际参数绑定到问号，执行并检查错误；prepare本身尚未完成读写。
                    // <<< MAIN-DEFENSE
                    q.prepare("INSERT INTO work_sources(warehouse_id,item_id,work_id) VALUES(?,?,?)");
                    q.addBindValue(warehouseId); q.addBindValue(itemId); q.addBindValue(workId); ok = q.exec();
                    if (!ok) break;
                    q.prepare("INSERT INTO demo_media_map VALUES(?,?,?,?)"); q.addBindValue(work.id); q.addBindValue(warehouseId); q.addBindValue(itemId); q.addBindValue(workId); ok = q.exec();
                    // >>> MAIN-DEFENSE | importNew｜处理阶段 09
                    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
                    // <<< MAIN-DEFENSE
                    if (ok) sources.insert(work.id, {warehouseId, itemId, workId, work.media});
                }
                for (const auto& event : fixture.events) {
                    if (!ok) break;
                    const bool view = event.action == "view"; const auto source = sources.value(event.work);
                    // >>> MAIN-DEFENSE | importNew｜处理阶段 10
                    // 准备参数化SQL，涉及 behavior_events。随后把实际参数绑定到问号，执行并检查错误；prepare本身尚未完成读写。
                    // <<< MAIN-DEFENSE
                    q.prepare("INSERT INTO behavior_events(user_id,event_id,action,query,occurred_at_utc,synthetic,public_eligible,work_id,warehouse_id,item_id,media_snapshot) VALUES(?,?,?,?,?,1,1,?,?,?,?)");
                    q.addBindValue(users.value(event.user)); q.addBindValue(event.id); q.addBindValue(event.action);
                    q.addBindValue(view ? QStringLiteral("") : event.query); q.addBindValue(event.time);
                    q.addBindValue(view ? QVariant(source.work) : QVariant()); q.addBindValue(view ? QVariant(source.warehouse) : QVariant());
                    q.addBindValue(view ? QVariant(source.item) : QVariant()); q.addBindValue(view ? QVariant(source.media) : QVariant());
                    ok = q.exec();
                }
                // >>> MAIN-DEFENSE | importNew｜处理阶段 11
                // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
                // <<< MAIN-DEFENSE
                if (ok) {
                    q.prepare("INSERT INTO demo_import_metadata VALUES(1,'media-demo-v1',?,?,?,?)");
                    q.addBindValue(QString::fromLatin1(fixture.digest)); q.addBindValue(fixture.users.size());
                    q.addBindValue(fixture.works.size()); q.addBindValue(fixture.events.size()); ok = q.exec();
                }
                // >>> MAIN-DEFENSE | importNew｜处理阶段 12
                // 提交这一批修改；必须检查提交结果，调用了commit不等于已经成功。
                // <<< MAIN-DEFENSE
                if (ok) ok = db.commit();
                if (!ok) db.rollback();
            }
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(connection);
    // >>> MAIN-DEFENSE | importNew｜处理阶段 13
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!ok) error = QStringLiteral("演示数据导入失败；事件写入已回滚。若创建了新文件，请改用新的演示数据库路径重试。");
    return ok;
}
}

// >>> MAIN-DEFENSE | 函数入口｜main
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：int main(int argc, char** argv) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv); QCoreApplication::setApplicationName("media_demo_import");
    QCommandLineParser parser; parser.addHelpOption();
    parser.addOption({"demo-db", "New or previously imported demo-only database path.", "path"});
    parser.addOption({"fixture", "Synthetic algorithm_fixture.json path.", "path"});
    parser.addOption({"demo-password-stdin", "Read the shared DEMO-ONLY password from standard input."});
    parser.process(app);
    QTextStream out(stdout), err(stderr);
    // >>> MAIN-DEFENSE | main｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!parser.isSet("demo-db") || !parser.isSet("fixture") || parser.value("demo-db").trimmed().isEmpty()) {
        err << "Both --demo-db and --fixture are required." << Qt::endl; return 2;
    }
    const QFileInfo target(parser.value("demo-db"));
    // >>> MAIN-DEFENSE | main｜处理阶段 02
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (target.isSymLink() || (target.exists() && !target.isFile())) { err << "Invalid demo database target." << Qt::endl; return 2; }
    Fixture fixture; QString error;
    if (!loadFixture(parser.value("fixture"), fixture, error)) { err << error << Qt::endl; return 1; }
    if (target.exists()) {
        // >>> MAIN-DEFENSE | main｜处理阶段 03
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!existingDemoMatches(target.absoluteFilePath(), fixture, error)) { err << error << Qt::endl; return 1; }
        out << "Matching demo already imported; no changes." << Qt::endl; return 0;
    }
    QByteArray password("demo-only-123");
    // >>> MAIN-DEFENSE | main｜处理阶段 04
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (parser.isSet("demo-password-stdin")) { QTextStream input(stdin); password = input.readLine().toUtf8(); }
    if (password.size() < 6 || password.size() > 1024 || sodium_init() < 0) { err << "Invalid demo password or crypto initialization." << Qt::endl; return 1; }
    if (!importNew(target.absoluteFilePath(), fixture, password, error)) { err << error << Qt::endl; return 1; }
    out << "Imported " << fixture.users.size() << " simulated users, " << fixture.works.size() << " works and " << fixture.events.size() << " events." << Qt::endl;
    // >>> MAIN-DEFENSE | main｜处理阶段 05
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return 0;
}
