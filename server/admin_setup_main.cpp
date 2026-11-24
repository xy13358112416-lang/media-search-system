#include "admin_setup_dialog.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QMessageBox>
#include <QSettings>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setFont(QFont(QStringLiteral("Microsoft YaHei UI")));
    QCoreApplication::setApplicationName(QStringLiteral("MediaServerAdminSetup"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("服务端本机首个管理员初始化工具"));
    parser.addHelpOption();
    const QCommandLineOption db("db", "服务器数据库路径。", "path");
    const QCommandLineOption config("config", "服务端配置文件，数据库路径相对配置文件解析。", "path");
    parser.addOption(db); parser.addOption(config); parser.process(app);
    const auto configPath = parser.isSet(config) ? QFileInfo(parser.value(config)).absoluteFilePath()
        : QDir(QCoreApplication::applicationDirPath()).filePath("media-server.ini");
    QString databasePath;
    if (parser.isSet(db)) {
        databasePath = parser.value(db).trimmed();
    } else if (QFileInfo::exists(configPath)) {
        QSettings settings(configPath, QSettings::IniFormat);
        const auto value = settings.value("server/database").toString().trimmed();
        if (settings.status() == QSettings::NoError && !value.isEmpty())
            databasePath = QDir(QFileInfo(configPath).absolutePath()).absoluteFilePath(value);
    } else if (!parser.isSet(config)) {
        databasePath = QDir(QCoreApplication::applicationDirPath()).filePath("warehouse.sqlite");
    }
    if (databasePath.isEmpty()) {
        QMessageBox::critical(nullptr, QStringLiteral("无法读取配置"),
            QStringLiteral("配置不存在或缺少有效的 server/database，请检查配置或使用 --db 明确指定数据库。"));
        return 1;
    }
    AdminSetupDialog dialog(databasePath);
    dialog.exec();
    return 0;
}
