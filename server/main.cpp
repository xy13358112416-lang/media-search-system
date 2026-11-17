#include "warehouse_server.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
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
    const QCommandLineOption config("config", "启动配置文件；默认读取 EXE 同目录的 media-server.ini。", "path");
    for (const auto& option : {address, port, database, initialize, user, config})
        parser.addOption(option);
    parser.process(app);
    QTextStream output(stdout);
    QTextStream errors(stderr);
    const QString configPath = parser.isSet(config) ? QFileInfo(parser.value(config)).absoluteFilePath()
        : QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("media-server.ini"));
    const bool useConfig = QFileInfo::exists(configPath);
    if (parser.isSet(config) && !useConfig) {
        errors << "启动配置文件不存在。" << Qt::endl;
        return 1;
    }
    QString listenAddress = parser.value(address), portText = parser.value(port);
    QString databasePath = parser.value(database);
    if (useConfig) {
        QSettings settings(configPath, QSettings::IniFormat);
        if (!parser.isSet(address)) listenAddress = settings.value("server/listen", listenAddress).toString();
        if (!parser.isSet(port)) portText = settings.value("server/port", portText).toString();
        if (!parser.isSet(database)) {
            if (!settings.contains("server/database")) {
                errors << "启动配置缺少 server/database。" << Qt::endl;
                return 1;
            }
            databasePath = settings.value("server/database", databasePath).toString().trimmed();
            if (!databasePath.isEmpty())
                databasePath = QDir(QFileInfo(configPath).absolutePath()).absoluteFilePath(databasePath);
        }
        if (settings.status() != QSettings::NoError || databasePath.isEmpty()) {
            errors << "启动配置文件无效。" << Qt::endl;
            return 1;
        }
    }
    bool portValid = false;
    const auto portNumber = portText.toUShort(&portValid);
    QHostAddress host;
    if (!portValid || (!portNumber && !parser.isSet(port)) || !host.setAddress(listenAddress)) {
        errors << "监听地址或端口无效。" << Qt::endl;
        return 1;
    }
    // A misplaced config must not quietly open a new, empty demonstration.
    if (useConfig && !parser.isSet(initialize) && !parser.isSet(user) && !QFileInfo::exists(databasePath)) {
        errors << "配置的数据库不存在；请先导入演示数据，或使用 --init 明确初始化。" << Qt::endl;
        return 1;
    }
    WarehouseServer server;
    QString error;
    if (!server.openDatabase(databasePath, error)) {
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

    if (!server.listen(host, portNumber)) {
        errors << server.errorString() << Qt::endl;
        return 1;
    }
    output << "仓库服务器已启动 " << host.toString() << ':' << server.serverPort() << Qt::endl;
    output << "数据库：" << QFileInfo(databasePath).absoluteFilePath() << Qt::endl;
    return app.exec();
}
