#include "warehouse_server.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("media_server"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("媒体仓库局域网服务器"));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("db"), QStringLiteral("服务器 SQLite 文件路径"), QStringLiteral("path"), QStringLiteral("warehouse_server.sqlite")});
    parser.addOption({QStringLiteral("listen"), QStringLiteral("监听 IP；局域网使用 0.0.0.0"), QStringLiteral("ip"), QStringLiteral("127.0.0.1")});
    parser.addOption({QStringLiteral("port"), QStringLiteral("监听端口"), QStringLiteral("port"), QStringLiteral("12345")});
    parser.addOption({QStringLiteral("init-admin"), QStringLiteral("仅在空数据库中创建首个账户；密码从标准输入读取"), QStringLiteral("username")});
    parser.addOption({QStringLiteral("add-user"), QStringLiteral("添加账户；密码从标准输入读取"), QStringLiteral("username")});
    parser.process(app);
    QTextStream out(stdout), err(stderr), in(stdin);
    if (parser.isSet("init-admin") && parser.isSet("add-user")) {
        err << "只能指定一种账户操作\n"; return 2;
    }
    WarehouseServer server;
    QString error;
    if (!server.open(parser.value("db"), error)) { err << "打开数据库失败: " << error << '\n'; return 1; }
    if (parser.isSet("init-admin") || parser.isSet("add-user")) {
        out << "请输入密码（输入可能显示在终端）： " << Qt::flush;
        const QString password = in.readLine();
        const QString username = parser.value(parser.isSet("init-admin") ? "init-admin" : "add-user");
        const bool created = parser.isSet("init-admin")
            ? server.addInitialUser(username, password, error) : server.addUser(username, password, error);
        if (!created) { err << "创建账户失败: " << error << '\n'; return 1; }
        out << "账户已创建\n"; return 0;
    }
    bool ok = false;
    const uint port = parser.value("port").toUInt(&ok);
    QHostAddress address;
    if (!ok || port == 0 || port > 65535 || !address.setAddress(parser.value("listen"))) {
        err << "监听 IP 或端口无效\n"; return 2;
    }
    if (!server.listen(address, static_cast<quint16>(port), error)) {
        err << "监听失败: " << error << '\n'; return 1;
    }
    out << "服务器监听 " << address.toString() << ':' << server.port() << Qt::endl;
    return app.exec();
}
