#include "warehouse_server.h"
#include <QDateTime>
#include <QJsonArray>
#include <QSqlQuery>
#include <QTcpSocket>
#include <sodium.h>

void WarehouseServer::revokeUserSessions(qint64 userId)
{
    const auto sockets = sessions_.keys();
    for (auto* socket : sockets) {
        const auto session = sessions_.value(socket);
        if (session && session->userId == userId) socket->abort();
    }
}

QJsonObject WarehouseServer::dispatchAdmin(Session& session, const QJsonObject& request)
{
    const auto type = request.value("type").toString();
    const auto payload = request.value("payload").toObject();
    const auto fail = [&](const char* code, const QString& message) {
        return QJsonObject{{"type", "error_resp"}, {"req_id", request.value("req_id")},
                           {"code", code}, {"message", message}};
    };
    const auto success = [&](QJsonObject value = {}) {
        return QJsonObject{{"type", type + "_resp"}, {"req_id", request.value("req_id")}, {"payload", value}};
    };
    QSqlQuery query(db_);
    query.prepare("SELECT is_admin FROM network_user_access WHERE user_id=? AND disabled=0");
    query.addBindValue(session.userId);
    if (!query.exec()) return fail("ServerError", QStringLiteral("无法读取权限。"));
    if (!query.next() || !query.value(0).toBool())
        return fail("PermissionDenied", QStringLiteral("需要管理员权限。"));
    query.finish();

    if (type == "admin_list_users") {
        bool valid = false;
        const int offset = payload.value("offset").toString().toInt(&valid);
        if (!valid || offset < 0) return fail("InvalidArgument", QStringLiteral("分页参数无效。"));
        query.prepare("SELECT u.id,u.username,COALESCE(a.is_admin,0),COALESCE(a.disabled,0) "
                      "FROM network_users u LEFT JOIN network_user_access a ON a.user_id=u.id "
                      "ORDER BY u.id LIMIT 101 OFFSET ?");
        query.addBindValue(offset);
        if (!query.exec()) return fail("ServerError", QStringLiteral("无法读取用户。"));
        QJsonArray users;
        bool more = false;
        while (query.next()) {
            if (users.size() == 100) { more = true; break; }
            users.append(QJsonObject{{"user_id", QString::number(query.value(0).toLongLong())},
                {"username", query.value(1).toString()}, {"is_admin", query.value(2).toBool()},
                {"disabled", query.value(3).toBool()}});
        }
        return success({{"users", users}, {"has_more", more}});
    }

    if (type == "admin_create_user") {
        if (!payload.value("username").isString() || !payload.value("password").isString())
            return fail("InvalidArgument", QStringLiteral("请输入用户名和密码。"));
        QString error;
        // Additional administrator accounts are provisioned at the server console.
        if (!addUser(payload.value("username").toString(), payload.value("password").toString(), error, false, session.userId))
            return fail("InvalidArgument", error);
        return success();
    }

    if (type != "admin_set_disabled" && type != "admin_reset_password")
        return fail("InvalidArgument", QStringLiteral("未知管理操作。"));
    bool valid = false;
    const auto target = payload.value("user_id").toString().toLongLong(&valid);
    if (!valid || target <= 0 || target == session.userId)
        return fail("InvalidArgument", QStringLiteral("请选择其他普通用户。"));
    query.prepare("SELECT COALESCE(a.is_admin,0) FROM network_users u LEFT JOIN network_user_access a "
                  "ON a.user_id=u.id WHERE u.id=?");
    query.addBindValue(target);
    if (!query.exec()) return fail("ServerError", QStringLiteral("无法读取用户。"));
    if (!query.next()) return fail("NotFound", QStringLiteral("用户不存在。"));
    if (query.value(0).toBool()) return fail("PermissionDenied", QStringLiteral("管理员账号由服务器控制台维护。"));
    query.finish();

    QString hash;
    if (type == "admin_reset_password") {
        const auto password = payload.value("password").toString().toUtf8();
        if (password.size() < 6 || password.size() > 1024)
            return fail("InvalidArgument", QStringLiteral("密码长度为 6–1024 字节。"));
        char encoded[crypto_pwhash_STRBYTES];
        if (crypto_pwhash_str(encoded, password.constData(), password.size(),
                crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0)
            return fail("ServerError", QStringLiteral("无法生成密码哈希。"));
        hash = QString::fromLatin1(encoded);
    } else if (!payload.value("disabled").isBool()) {
        return fail("InvalidArgument", QStringLiteral("账号状态无效。"));
    }
    if (!db_.transaction()) return fail("ServerError", QStringLiteral("无法开始事务。"));
    if (type == "admin_reset_password") {
        query.prepare("UPDATE network_users SET password_hash=? WHERE id=?");
        query.addBindValue(hash); query.addBindValue(target);
    } else {
        query.prepare("INSERT INTO network_user_access(user_id,disabled) VALUES (?,?) "
                      "ON CONFLICT(user_id) DO UPDATE SET disabled=excluded.disabled");
        query.addBindValue(target); query.addBindValue(payload.value("disabled").toBool());
    }
    bool saved = query.exec();
    query.prepare("INSERT INTO admin_audit(actor_id,target_id,action,occurred_at_utc) VALUES (?,?,?,?)");
    query.addBindValue(session.userId); query.addBindValue(target);
    query.addBindValue(type == "admin_set_disabled"
        ? (payload.value("disabled").toBool() ? "disable_user" : "enable_user") : type);
    query.addBindValue(QDateTime::currentSecsSinceEpoch());
    if (!saved || !query.exec() || !db_.commit()) {
        db_.rollback(); return fail("ServerError", QStringLiteral("用户修改未保存。"));
    }
    revokeUserSessions(target);
    return success();
}
