// >>> MAIN-DEFENSE | 主逻辑文件导读
// 在线服务器进程入口：解析命令行和INI路径，打开明确指定数据库，可初始化/建账号后退出，也可监听地址端口并进入Qt事件循环。
// 阅读时先看函数入口说明，再按“处理阶段”往下；到每个return核对成功、空结果和错误是否区别。
// <<< MAIN-DEFENSE
#include "warehouse_server.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QTextStream>

// >>> MAIN-DEFENSE | 函数入口｜main
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：int main(int argc, char** argv)  
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
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
    // >>> MAIN-DEFENSE | main｜处理阶段 01
    // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
    // <<< MAIN-DEFENSE
    for (const auto& option : {address, port, database, initialize, user, config})
        parser.addOption(option);
    parser.process(app);
    QTextStream output(stdout);
    QTextStream errors(stderr);
    const QString configPath = parser.isSet(config) ? QFileInfo(parser.value(config)).absoluteFilePath()
        : QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("media-server.ini"));
    const bool useConfig = QFileInfo::exists(configPath);
    // >>> MAIN-DEFENSE | main｜处理阶段 02
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (parser.isSet(config) && !useConfig) {
        errors << "启动配置文件不存在。" << Qt::endl;
        return 1;
    }
    QString listenAddress = parser.value(address), portText = parser.value(port);
    QString databasePath = parser.value(database);
    // >>> MAIN-DEFENSE | main｜处理阶段 03
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (useConfig) {
        QSettings settings(configPath, QSettings::IniFormat);
        if (!parser.isSet(address)) listenAddress = settings.value("server/listen", listenAddress).toString();
        if (!parser.isSet(port)) portText = settings.value("server/port", portText).toString();
        // >>> MAIN-DEFENSE | main｜处理阶段 04
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!parser.isSet(database)) {
            if (!settings.contains("server/database")) {
                errors << "启动配置缺少 server/database。" << Qt::endl;
                return 1;
            }
            databasePath = settings.value("server/database", databasePath).toString().trimmed();
            // >>> MAIN-DEFENSE | main｜处理阶段 05
            // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
            // <<< MAIN-DEFENSE
            if (!databasePath.isEmpty())
                databasePath = QDir(QFileInfo(configPath).absolutePath()).absoluteFilePath(databasePath);
        }
        if (settings.status() != QSettings::NoError || databasePath.isEmpty()) {
            errors << "启动配置文件无效。" << Qt::endl;
            // >>> MAIN-DEFENSE | main｜处理阶段 06
            // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
            // <<< MAIN-DEFENSE
            return 1;
        }
    }
    bool portValid = false;
    const auto portNumber = portText.toUShort(&portValid);
    QHostAddress host;
    // >>> MAIN-DEFENSE | main｜处理阶段 07
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!portValid || (!portNumber && !parser.isSet(port)) || !host.setAddress(listenAddress)) {
        errors << "监听地址或端口无效。" << Qt::endl;
        return 1;
    }
    // A misplaced config must not quietly open a new, empty demonstration.
    // >>> MAIN-DEFENSE | main｜处理阶段 08
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (useConfig && !parser.isSet(initialize) && !parser.isSet(user) && !QFileInfo::exists(databasePath)) {
        errors << "配置的数据库不存在；请先导入演示数据，或使用 --init 明确初始化。" << Qt::endl;
        return 1;
    }
    WarehouseServer server;
    QString error;
    // >>> MAIN-DEFENSE | main｜处理阶段 09
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!server.openDatabase(databasePath, error)) {
        errors << error << Qt::endl;
        return 1;
    }
    // >>> MAIN-DEFENSE | main｜处理阶段 10
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (parser.isSet(user)) {
        QTextStream input(stdin);
        output << "请输入演示账号的密码后回车：" << Qt::endl;
        if (!server.addUser(parser.value(user), input.readLine(), error)) {
            errors << error << Qt::endl;
            // >>> MAIN-DEFENSE | main｜处理阶段 11
            // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
            // <<< MAIN-DEFENSE
            return 1;
        }
        output << "已创建演示账号。" << Qt::endl;
        return 0;
    }
    // >>> MAIN-DEFENSE | main｜处理阶段 12
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (parser.isSet(initialize)) {
        output << "数据库已初始化。" << Qt::endl;
        return 0;
    }

    // >>> MAIN-DEFENSE | main｜处理阶段 13
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!server.listen(host, portNumber)) {
        errors << server.errorString() << Qt::endl;
        return 1;
    }
    output << "仓库服务器已启动 " << host.toString() << ':' << server.serverPort() << Qt::endl;
    output << "数据库：" << QFileInfo(databasePath).absoluteFilePath() << Qt::endl;
    // >>> MAIN-DEFENSE | main｜处理阶段 14
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return app.exec();
}
