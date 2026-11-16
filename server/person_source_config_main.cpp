#include "person_source_config.h"
#include "person_feed_refresh.h"
#include "person_store_sqlite.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFileInfo>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTextStream>

namespace {
care_contract::Status refreshPerson(QSqlDatabase& db, const QString& personId)
{
    person_store::SqliteStore store(db);
    QNetworkAccessManager network;
    care_contract::PageRequest request{0, care_contract::MaxPageSize};
    care_contract::Status firstFailure;
    do {
        const auto page = person_source_config::listSourcesForPerson(db, personId, request);
        if (!page.status.ok())
            return page.status;
        if (!page.value || (request.offset == 0 && page.value->items.isEmpty()))
            return {warehouse_contract::ErrorCode::NotFound, QStringLiteral("没有为该人物登记资讯源。")};
        for (const auto& source : page.value->items) {
            QEventLoop loop;
            bool finished = false;
            person_feed_refresh::refresh(store, network, source, [&](care_contract::Status status) {
                if (!status.ok() && firstFailure.ok())
                    firstFailure = std::move(status);
                finished = true;
                loop.quit();
            });
            if (!finished)
                loop.exec();
        }
        if (!page.value->nextOffset)
            return firstFailure;
        request.offset = *page.value->nextOffset;
    } while (true);
}
} // namespace

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
    const QCommandLineOption refresh(
        "refresh-person", "Fetch the registered HTTPS feeds for this stable person ID.", "person-id");
    parser.addOptions({database, config, refresh});
    parser.process(app);
    const auto fail = [](const QString& message) {
        QTextStream(stderr) << message << Qt::endl;
        return 1;
    };
    if (parser.value(database).trimmed().isEmpty() || parser.isSet(config) == parser.isSet(refresh))
        return fail("Specify --db and exactly one of --config or --refresh-person.");
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
            } else if (parser.isSet(refresh)) {
                const auto status = refreshPerson(db, parser.value(refresh));
                if (!status.ok()) code = fail(status.message);
                else QTextStream(stdout) << "Person feeds refreshed." << Qt::endl;
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
