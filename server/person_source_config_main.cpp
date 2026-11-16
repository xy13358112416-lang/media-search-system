#include "person_source_config.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTextStream>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("media_person_source_config");
    QCommandLineParser parser;
    parser.setApplicationDescription(
        "Register operator-reviewed person HTTPS feeds in an existing server database.");
    parser.addHelpOption();
    const QCommandLineOption database(
        "db", "Existing server SQLite database with person schema version 3.", "path");
    const QCommandLineOption config(
        "config", "care-person-source-config-v1 JSON file.", "path");
    parser.addOptions({database, config});
    parser.process(app);
    const auto fail = [](const QString& message) {
        QTextStream(stderr) << message << Qt::endl;
        return 1;
    };
    if (parser.value(database).trimmed().isEmpty() || parser.value(config).trimmed().isEmpty())
        return fail("Both --db and --config are required.");
    if (!QFileInfo(parser.value(database)).isFile())
        return fail("Server database does not exist; initialize it with media_server first.");
    int code = 0;
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", "person-source-operator");
        db.setDatabaseName(QFileInfo(parser.value(database)).absoluteFilePath());
        db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=5000");
        if (!db.open()) {
            code = fail("Cannot open server database.");
        } else {
            QSqlQuery query(db);
            if (!query.exec("PRAGMA foreign_keys=ON")) {
                code = fail("Cannot enable foreign key checks.");
            } else {
                const auto result = person_source_config::registerFile(db, parser.value(config));
                if (!result.status.ok() || !result.value) {
                    code = fail(result.status.message);
                } else {
                    const QJsonObject report{{"inserted", result.value->inserted},
                                             {"unchanged", result.value->unchanged}};
                    QTextStream(stdout)
                        << QJsonDocument(report).toJson(QJsonDocument::Compact) << Qt::endl;
                }
            }
        }
        db.close();
    }
    QSqlDatabase::removeDatabase("person-source-operator");
    return code;
}
