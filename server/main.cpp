#include "warehouse_server.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("media_server"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("多媒体资源仓库服务器"));
    parser.addHelpOption();
    const QCommandLineOption address("listen", "监听地址。局域网演示时可指定服务器的局域网地址。", "address", "127.0.0.1");
    const QCommandLineOption port("port", "监听端口。", "port", "8765");
    const QCommandLineOption database("db", "仓库独立 SQLite 路径。", "path", "warehouse.sqlite");
    const QCommandLineOption initialize("init", "初始化数据库后退出。");
    const QCommandLineOption user("add-user", "从标准输入读取密码，创建演示账号后退出。", "username");
    for (const auto& option : {address, port, database, initialize, user})
        parser.addOption(option);
    parser.process(app);
    QTextStream output(stdout);
    QTextStream errors(stderr);
    WarehouseServer server;
    QString error;
    if (!server.openDatabase(parser.value(database), error)) {
        errors << error << Qt::endl;
        return 1;
    }
    if (parser.isSet(user)) {
        QTextStream input(stdin);
        output << "请输入演示账号的密码后回车：" << Qt::endl;
        if (!server.addUser(parser.value(user), input.readLine(), error)) {
            errors << error << Qt::endl;
            return 1;
        }
        output << "已创建演示账号。" << Qt::endl;
        return 0;
    }
    if (parser.isSet(initialize)) {
        output << "数据库已初始化。" << Qt::endl;
        return 0;
    }

    bool portValid = false;
    const quint16 portNumber = parser.value(port).toUShort(&portValid);
    QHostAddress host;
    if (!portValid || !host.setAddress(parser.value(address))) {
        errors << "监听地址或端口无效。" << Qt::endl;
        return 1;
    }
    if (!server.listen(host, portNumber)) {
        errors << server.errorString() << Qt::endl;
        return 1;
    }
    output << "仓库服务器已启动 " << host.toString() << ':' << server.serverPort() << Qt::endl;
    return app.exec();
}
