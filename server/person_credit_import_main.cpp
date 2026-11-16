#include "person_credit_import.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTextStream>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("media_person_import");
    QCommandLineParser parser;
    parser.setApplicationDescription("Import reviewed person credits into an existing server database (schema 3). Stop the server before operator imports.");
    parser.addHelpOption();
    parser.addOption({"allow-synthetic", "Explicitly allow fictional fixtures in a dedicated demo/test database."});
    parser.addPositionalArgument("database", "Existing server SQLite database.");
    parser.addPositionalArgument("mapping", "Reviewed care-person-import-v1 JSON file.");
    parser.process(app);
    const auto args = parser.positionalArguments();
    if (args.size() != 2) parser.showHelp(2);
    const auto fail = [](const QString& message) { QTextStream(stderr) << message << Qt::endl; return 1; };
    if (!QFileInfo(args[0]).isFile()) return fail("Server database does not exist; initialize it with media_server first.");
    QFile file(args[1]);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 8 * 1024 * 1024)
        return fail("Cannot read mapping file, or it exceeds 8 MiB.");
    const auto batch = person_credit_import::parse(file.readAll());
    if (!batch.status.ok() || !batch.value) return fail(batch.status.message);
    int code = 0;
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", "person-operator-import");
        db.setDatabaseName(QFileInfo(args[0]).absoluteFilePath());
        db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=5000");
        if (!db.open()) code = fail("Cannot open server database.");
        else {
            QSqlQuery query(db);
            if (!query.exec("PRAGMA foreign_keys=ON")) code = fail("Cannot enable foreign key checks.");
            else {
                const auto result = person_credit_import::importBatch(db, *batch.value, {parser.isSet("allow-synthetic")});
                if (!result.status.ok() || !result.value) code = fail(result.status.message);
                else {
                    const QJsonObject report{{"synthetic", batch.value->synthetic},
                        {"people_inserted", result.value->peopleInserted},
                        {"aliases_inserted", result.value->aliasesInserted},
                        {"credits_inserted", result.value->creditsInserted}};
                    QTextStream(stdout) << QJsonDocument(report).toJson(QJsonDocument::Compact) << Qt::endl;
                }
            }
        }
        db.close();
    }
    QSqlDatabase::removeDatabase("person-operator-import");
    return code;
}
