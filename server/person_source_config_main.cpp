#include "person_source_config.h"
#include "person_feed_refresh.h"
#include "deepseek_person_news.h"
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
    bool hasFeeds = false;
    do {
        const auto page = person_source_config::listSourcesForPerson(db, personId, request);
        if (!page.status.ok())
            return page.status;
        if (!page.value)
            return {warehouse_contract::ErrorCode::ServerError, QStringLiteral("无法读取资讯源配置。")};
        for (const auto& source : page.value->items) {
            if (deepseek_person_news::isSearchSource(source.feedUrl))
                continue;
            hasFeeds = true;
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
        if (!page.value->nextOffset) {
            if (!hasFeeds)
                return {warehouse_contract::ErrorCode::NotFound, QStringLiteral("没有为该人物登记 RSS 或 Atom。")};
            return firstFailure;
        }
        request.offset = *page.value->nextOffset;
    } while (true);
}
care_contract::Status refreshDeepSeek(QSqlDatabase& db, const QString& personId,
                                     const QString& approvedSite, const QString& identityClue)
{
    using warehouse_contract::ErrorCode;
    const auto id = care_contract::validatePersonId(personId);
    if (!id.ok())
        return id;
    const auto key = qgetenv("MEDIA_SERVER_DEEPSEEK_API_KEY").trimmed();
    if (key.isEmpty() || key.size() > 4096 || key.contains('\n') || key.contains('\r') || key.contains('\0'))
        return {ErrorCode::InvalidArgument, QStringLiteral("请在服务器进程设置 MEDIA_SERVER_DEEPSEEK_API_KEY。")};
    QSqlQuery row(db);
    row.prepare("SELECT display_name FROM person_catalog WHERE person_id=?");
    row.addBindValue(personId);
    if (!row.exec())
        return {ErrorCode::ServerError, QStringLiteral("无法读取可信人物目录。")};
    if (!row.next())
        return {ErrorCode::NotFound, QStringLiteral("人物未导入可信目录。")};
    const deepseek_person_news::Request input{personId, row.value(0).toString(), approvedSite, identityClue};
    row.finish();
    const auto source = deepseek_person_news::cacheSource(input);
    if (!source.status.ok())
        return source.status;
    if (!source.value)
        return {ErrorCode::ServerError, QStringLiteral("无法建立 DeepSeek 缓存来源。")};
    const auto registered = person_source_config::registerBatch(db, {
        QStringLiteral("Publisher and identity reviewed by server operator."),
        {{personId, *source.value}}
    });
    if (!registered.status.ok())
        return registered.status;

    person_store::SqliteStore store(db);
    QNetworkAccessManager network;
    QEventLoop loop;
    std::optional<care_contract::Status> result;
    deepseek_person_news::refresh(store, network, input, key, [&](care_contract::Status status) {
        result = std::move(status);
        loop.quit();
    });
    if (!result)
        loop.exec();
    return result.value_or(care_contract::Status{ErrorCode::ServerError, QStringLiteral("资讯刷新已中止。")});
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("media_person_source_config");
    QCommandLineParser parser;
    parser.setApplicationDescription(
        "Register or refresh operator-reviewed person news sources in an existing server database.");
    parser.addHelpOption();
    const QCommandLineOption database(
        "db", "Existing server SQLite database with person schema version 3.", "path");
    const QCommandLineOption config(
        "config", "care-person-source-config-v1 JSON file.", "path");
    const QCommandLineOption refresh(
        "refresh-person", "Fetch the registered HTTPS feeds for this stable person ID.", "person-id");
    const QCommandLineOption deepseek("deepseek-person", "Refresh DeepSeek news for a verified person ID.", "person-id");
    const QCommandLineOption site("news-site", "Independently verified public publisher hostname.", "hostname");
    const QCommandLineOption clue("news-clue", "Public identity clue such as a verified work title.", "text");
    parser.addOptions({database, config, refresh, deepseek, site, clue});
    parser.process(app);
    const auto fail = [](const QString& message) {
        QTextStream(stderr) << message << Qt::endl;
        return 1;
    };
    const int selected = int(parser.isSet(config)) + int(parser.isSet(refresh)) + int(parser.isSet(deepseek));
    if (parser.value(database).trimmed().isEmpty() || selected != 1)
        return fail("Specify --db and exactly one of --config, --refresh-person or --deepseek-person.");
    if ((parser.isSet(deepseek) && (!parser.isSet(site) || !parser.isSet(clue))) ||
        (!parser.isSet(deepseek) && (parser.isSet(site) || parser.isSet(clue))))
        return fail("--deepseek-person requires both --news-site and --news-clue.");
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
            } else if (parser.isSet(deepseek)) {
                const auto status = refreshDeepSeek(db, parser.value(deepseek), parser.value(site), parser.value(clue));
                if (!status.ok()) code = fail(status.message);
                else QTextStream(stdout) << "DeepSeek person news refreshed." << Qt::endl;
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
