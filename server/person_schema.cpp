// >>> MAIN-DEFENSE | 主逻辑文件导读
// 人物数据库迁移：核对版本、列、主键/外键/约束；在事务内升级到受支持结构。版本号相同也要查结构，不能假定同名表必然正确。
// 阅读时先看函数入口说明，再按“处理阶段”往下；到每个return核对成功、空结果和错误是否区别。
// <<< MAIN-DEFENSE
#include "person_schema.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QMap>
#include <QSet>
#include <QStringList>

namespace person_schema {
// >>> MAIN-DEFENSE | 函数入口｜migrate
// 验证SQLite版本和人物表结构，在事务中执行升级；失败回滚，不伪装空库继续运行。
// 签名提示：bool migrate(QSqlDatabase& db, QString& error) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool migrate(QSqlDatabase& db, QString& error) {
    error.clear();
    if (!db.isOpen() || db.driverName() != "QSQLITE") {
        error = QStringLiteral("人物迁移需要已打开的 SQLite 连接。"); return false;
    }
    QSqlQuery q(db);
    // >>> MAIN-DEFENSE | migrate｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!q.exec("PRAGMA foreign_keys") || !q.next() || q.value(0).toInt() != 1) {
        error = QStringLiteral("人物迁移要求启用 SQLite 外键检查。"); return false;
    }
    q.finish();
    // >>> MAIN-DEFENSE | migrate｜处理阶段 02
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!q.exec("SELECT COALESCE(MAX(version),0) FROM server_schema_migrations") || !q.next()) {
        error = QStringLiteral("无法读取服务器数据库版本。"); return false;
    }
    bool validVersion = false;
    const qint64 version = q.value(0).toLongLong(&validVersion); q.finish();
    // >>> MAIN-DEFENSE | migrate｜处理阶段 03
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!validVersion || (version != 1 && version != 2 && version != 3)) {
        error = QStringLiteral("服务器数据库版本不受此迁移支持。"); return false;
    }
    const QStringList statements{
        "CREATE TABLE person_catalog(person_id TEXT PRIMARY KEY NOT NULL,display_name TEXT NOT NULL,identity_source TEXT NOT NULL)",
        "CREATE TABLE person_aliases(person_id TEXT NOT NULL REFERENCES person_catalog(person_id),alias TEXT NOT NULL,PRIMARY KEY(person_id,alias))",
        "CREATE TABLE person_credits(person_id TEXT NOT NULL REFERENCES person_catalog(person_id),work_id TEXT NOT NULL,role TEXT NOT NULL CHECK(role IN ('actor','director','musician')),credit_source TEXT NOT NULL,PRIMARY KEY(person_id,work_id,role))",
        "CREATE INDEX person_credits_work ON person_credits(work_id,person_id)",
        "CREATE TABLE person_follows(user_id INTEGER NOT NULL REFERENCES network_users(id),person_id TEXT NOT NULL REFERENCES person_catalog(person_id),known_name TEXT NOT NULL,known_roles_json BLOB NOT NULL,PRIMARY KEY(user_id,person_id))",
        "CREATE TABLE person_sources(person_id TEXT NOT NULL REFERENCES person_catalog(person_id),source_url TEXT NOT NULL,last_success_utc INTEGER,stale INTEGER NOT NULL DEFAULT 0 CHECK(stale IN (0,1)),refresh_generation INTEGER NOT NULL DEFAULT 0 CHECK(refresh_generation>=0),PRIMARY KEY(person_id,source_url))",
        "CREATE TABLE person_updates(person_id TEXT NOT NULL,update_id TEXT NOT NULL,feed_url TEXT NOT NULL,source_url TEXT NOT NULL,title TEXT NOT NULL,kind TEXT NOT NULL CHECK(kind IN ('news','release')),published_at_utc INTEGER,event_at_utc INTEGER,fetched_at_utc INTEGER NOT NULL,stale INTEGER NOT NULL DEFAULT 0 CHECK(stale IN (0,1)),PRIMARY KEY(person_id,update_id,feed_url),FOREIGN KEY(person_id,feed_url) REFERENCES person_sources(person_id,source_url))",
        "INSERT INTO server_schema_migrations(version) VALUES(2)",
        "INSERT INTO server_schema_migrations(version) VALUES(3)"
    };
    const auto normalized = [](const QString& sql) {
        QString output; bool quoted = false;
        // >>> MAIN-DEFENSE | migrate｜处理阶段 04
        // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
        // <<< MAIN-DEFENSE
        for (qsizetype i = 0; i < sql.size(); ++i) {
            const auto ch = sql.at(i);
            if (ch == QLatin1Char('\'')) {
                output += ch;
                // >>> MAIN-DEFENSE | migrate｜处理阶段 05
                // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
                // <<< MAIN-DEFENSE
                if (quoted && i + 1 < sql.size() && sql.at(i + 1) == QLatin1Char('\'')) output += sql.at(++i);
                else quoted = !quoted;
            } else if (quoted) output += ch;
            else if (ch == QLatin1Char('"') || ch == QLatin1Char('`') || ch == QLatin1Char('[') || ch == QLatin1Char(']')) continue;
            else if (!ch.isSpace()) output += ch.toLower();
        }
        // >>> MAIN-DEFENSE | migrate｜处理阶段 06
        // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
        // <<< MAIN-DEFENSE
        return output;
    };
    const QMap<QString, QSet<QString>> requiredV2{
        {"person_catalog", {"person_id", "display_name", "identity_source"}},
        {"person_aliases", {"person_id", "alias"}},
        {"person_credits", {"person_id", "work_id", "role", "credit_source"}},
        {"person_follows", {"user_id", "person_id", "known_name", "known_roles_json"}},
        {"person_sources", {"person_id", "source_url", "last_success_utc", "stale"}},
        {"person_updates", {"person_id", "update_id", "feed_url", "source_url", "title", "kind", "published_at_utc", "event_at_utc", "fetched_at_utc", "stale"}}
    };
    auto requiredV3 = requiredV2;
    requiredV3["person_sources"].insert("refresh_generation");
    const auto& required = version == 3 ? requiredV3 : requiredV2;
    // >>> MAIN-DEFENSE | migrate｜处理阶段 07
    // 准备本步骤的内存查找、去重或汇总容器；它们不是数据库表，生命周期通常只在本次调用内。
    // <<< MAIN-DEFENSE
    const QMap<QString, QStringList> primaryKeys{
        {"person_catalog", {"person_id"}}, {"person_aliases", {"person_id", "alias"}},
        {"person_credits", {"person_id", "work_id", "role"}}, {"person_follows", {"user_id", "person_id"}},
        {"person_sources", {"person_id", "source_url"}}, {"person_updates", {"person_id", "update_id", "feed_url"}}
    };
    // >>> MAIN-DEFENSE | migrate｜处理阶段 08
    // 准备本步骤的内存查找、去重或汇总容器；它们不是数据库表，生命周期通常只在本次调用内。
    // <<< MAIN-DEFENSE
    const QMap<QString, QSet<QString>> nullable{
        {"person_sources", {"last_success_utc"}}, {"person_updates", {"published_at_utc", "event_at_utc"}}
    };
    const QSet<QString> integerColumns{"user_id", "last_success_utc", "stale", "refresh_generation",
                                       "published_at_utc", "event_at_utc", "fetched_at_utc"};
    // >>> MAIN-DEFENSE | migrate｜处理阶段 09
    // 准备本步骤的内存查找、去重或汇总容器；它们不是数据库表，生命周期通常只在本次调用内。
    // <<< MAIN-DEFENSE
    const QSet<QString> blobColumns{"known_roles_json", "people_json"};
    const QMap<QString, QStringList> requiredChecksV2{
        {"person_credits", {"check(rolein('actor','director','musician'))"}},
        {"person_sources", {"check(stalein(0,1))"}},
        {"person_updates", {"check(kindin('news','release'))", "check(stalein(0,1))"}}
    };
    auto requiredChecksV3 = requiredChecksV2;
    requiredChecksV3["person_sources"].append("check(refresh_generation>=0)");
    const auto& requiredChecks = version == 3 ? requiredChecksV3 : requiredChecksV2;
    // >>> MAIN-DEFENSE | migrate｜处理阶段 10
    // 准备本步骤的内存查找、去重或汇总容器；它们不是数据库表，生命周期通常只在本次调用内。
    // <<< MAIN-DEFENSE
    const QMap<QString, QSet<QString>> foreignKeys{
        {"person_catalog", {}},
        {"person_aliases", {"person_catalog|person_id|person_id|NO ACTION"}},
        {"person_credits", {"person_catalog|person_id|person_id|NO ACTION"}},
        {"person_follows", {"network_users|user_id|id|NO ACTION", "person_catalog|person_id|person_id|NO ACTION"}},
        {"person_sources", {"person_catalog|person_id|person_id|NO ACTION"}},
        {"person_updates", {"person_sources|person_id|person_id|NO ACTION", "person_sources|feed_url|source_url|NO ACTION"}}
    };
    // >>> MAIN-DEFENSE | migrate｜处理阶段 11
    // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
    // <<< MAIN-DEFENSE
    for (auto it = required.cbegin(); it != required.cend(); ++it) {
        q.prepare("SELECT type,sql FROM sqlite_master WHERE name=?"); q.addBindValue(it.key());
        if (!q.exec()) { error = QStringLiteral("无法检查人物数据库结构。"); return false; }
        const bool exists = q.next(); const QString type = exists ? q.value(0).toString() : QString();
        const QString definition = exists ? q.value(1).toString() : QString(); q.finish();
        // >>> MAIN-DEFENSE | migrate｜处理阶段 12
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (version == 1 && exists) { error = QStringLiteral("人物表名已被占用，未修改数据库。"); return false; }
        if (version >= 2) {
            if (!exists || type != "table") {
                error = QStringLiteral("人物数据库结构不完整。"); return false;
            }
            // >>> MAIN-DEFENSE | migrate｜处理阶段 13
            // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
            // <<< MAIN-DEFENSE
            for (const auto& check : requiredChecks.value(it.key()))
                if (!normalized(definition).contains(check)) { error = QStringLiteral("人物数据库检查约束不匹配。"); return false; }
            if (!q.exec("PRAGMA table_info(" + it.key() + ")")) { error = QStringLiteral("无法读取人物数据库字段。"); return false; }
            QSet<QString> columns; QMap<int, QString> primary;
            // >>> MAIN-DEFENSE | migrate｜处理阶段 14
            // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
            // <<< MAIN-DEFENSE
            while (q.next()) {
                const auto name = q.value(1).toString(); columns.insert(name);
                const auto expectedType = integerColumns.contains(name) ? "INTEGER" : blobColumns.contains(name) ? "BLOB" : "TEXT";
                if (q.value(2).toString().toUpper() != expectedType ||
                    (name == "stale" || name == "refresh_generation"
                         ? q.value(4).toString() != "0" : !q.value(4).isNull())) {
                    error = QStringLiteral("人物数据库字段类型或默认值不匹配。"); return false;
                }
                const int keyPosition = q.value(5).toInt(); if (keyPosition > 0) primary.insert(keyPosition, name);
                // >>> MAIN-DEFENSE | migrate｜处理阶段 15
                // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
                // <<< MAIN-DEFENSE
                if (it.value().contains(name) &&
                    (q.value(3).toInt() == 0) != nullable.value(it.key()).contains(name)) {
                    error = QStringLiteral("人物数据库非空约束不匹配。"); return false;
                }
            }
            // >>> MAIN-DEFENSE | migrate｜处理阶段 16
            // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
            // <<< MAIN-DEFENSE
            if (q.lastError().isValid()) { error = QStringLiteral("无法读取人物数据库字段。"); return false; }
            if (columns != it.value()) { error = QStringLiteral("人物数据库字段不匹配。"); return false; }
            if (primary.values() != primaryKeys.value(it.key())) { error = QStringLiteral("人物数据库主键不匹配。"); return false; }
            q.finish();
            // >>> MAIN-DEFENSE | migrate｜处理阶段 17
            // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
            // <<< MAIN-DEFENSE
            if (!q.exec("PRAGMA foreign_key_list(" + it.key() + ")")) { error = QStringLiteral("无法读取人物外键。"); return false; }
            QSet<QString> actualForeign; QMap<int, int> foreignGroups;
            while (q.next()) {
                actualForeign.insert(q.value(2).toString() + "|" + q.value(3).toString() + "|" + q.value(4).toString() + "|" + q.value(6).toString().toUpper());
                // >>> MAIN-DEFENSE | migrate｜处理阶段 18
                // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
                // <<< MAIN-DEFENSE
                if (q.value(5).toString().toUpper() != "NO ACTION") { error = QStringLiteral("人物数据库外键更新规则不匹配。"); return false; }
                ++foreignGroups[q.value(0).toInt()];
            }
            if (q.lastError().isValid() || actualForeign != foreignKeys.value(it.key()) ||
                ((it.key() == "person_updates") &&
                 (foreignGroups.size() != 1 || foreignGroups.first() != 2))) {
                error = QStringLiteral("人物数据库外键不匹配。"); return false;
            }
            q.finish();
        }
    }
    // >>> MAIN-DEFENSE | migrate｜处理阶段 19
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (version >= 2) {
        q.prepare("SELECT tbl_name FROM sqlite_master WHERE type='index' AND name='person_credits_work'");
        if (!q.exec() || !q.next() || q.value(0).toString() != "person_credits") {
            error = QStringLiteral("人物数据库索引定义不匹配。"); return false;
        }
        q.finish();
        // >>> MAIN-DEFENSE | migrate｜处理阶段 20
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!q.exec("PRAGMA index_list(person_credits)")) { error = QStringLiteral("无法读取人物索引。"); return false; }
        bool foundIndex = false;
        while (q.next()) if (q.value(1).toString() == "person_credits_work") {
            foundIndex = q.value(2).toInt() == 0 && q.value(4).toInt() == 0;
        }
        // >>> MAIN-DEFENSE | migrate｜处理阶段 21
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (q.lastError().isValid() || !foundIndex) { error = QStringLiteral("人物数据库索引属性不匹配。"); return false; }
        q.finish();
        if (!q.exec("PRAGMA index_info(person_credits_work)")) { error = QStringLiteral("无法检查人物索引。"); return false; }
        QStringList indexed;
        // >>> MAIN-DEFENSE | migrate｜处理阶段 22
        // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
        // <<< MAIN-DEFENSE
        while (q.next()) indexed.append(q.value(2).toString());
        if (q.lastError().isValid() || indexed != QStringList{"work_id", "person_id"}) {
            error = QStringLiteral("人物数据库索引不匹配。"); return false;
        }
        // >>> MAIN-DEFENSE | migrate｜处理阶段 23
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (version == 3) return true;
        if (!q.exec("ALTER TABLE person_sources ADD COLUMN "
                    "refresh_generation INTEGER NOT NULL DEFAULT 0 CHECK(refresh_generation>=0)") ||
            !q.exec("INSERT INTO server_schema_migrations(version) VALUES(3)")) {
            error = QStringLiteral("无法升级人物资讯刷新代次，已保留原数据。"); return false;
        }
        // >>> MAIN-DEFENSE | migrate｜处理阶段 24
        // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
        // <<< MAIN-DEFENSE
        return true;
    }
    for (const auto& sql : statements) {
        if (!q.exec(sql)) { error = QStringLiteral("无法创建人物数据库结构，已保留原数据。"); return false; }
    }
    // >>> MAIN-DEFENSE | migrate｜处理阶段 25
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return true;
}
}
