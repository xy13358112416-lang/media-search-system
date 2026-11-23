// >>> MAIN-DEFENSE | 主逻辑文件导读
// 资讯来源运维CLI：登记配置、列出源或触发刷新，服务器环境提供Key；Qt不接收Key。
// 阅读时先看函数入口说明，再按“处理阶段”往下；到每个return核对成功、空结果和错误是否区别。
// <<< MAIN-DEFENSE
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
// >>> MAIN-DEFENSE | 函数入口｜refreshPerson
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：care_contract::Status refreshPerson(QSqlDatabase& db, const QString& personId)

// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
care_contract::Status refreshPerson(QSqlDatabase& db, const QString& personId)
{
    person_store::SqliteStore store(db);
    QNetworkAccessManager network;
    care_contract::PageRequest request{0, care_contract::MaxPageSize};
    care_contract::Status firstFailure;
    bool hasFeeds = false;
    do {
        const auto page = person_source_config::listSourcesForPerson(db, personId, request);
        // >>> MAIN-DEFENSE | refreshPerson｜处理阶段 01
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!page.status.ok())
            return page.status;
        if (!page.value)
            return {warehouse_contract::ErrorCode::ServerError, QStringLiteral("无法读取资讯源配置。")};
        // >>> MAIN-DEFENSE | refreshPerson｜处理阶段 02
        // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
        // <<< MAIN-DEFENSE
        for (const auto& source : page.value->items) {
            if (deepseek_person_news::isSearchSource(source.feedUrl))
                continue;
            hasFeeds = true;
            QEventLoop loop;
            bool finished = false;
            person_feed_refresh::refresh(store, network, source, [&](care_contract::Status status) {
                // >>> MAIN-DEFENSE | refreshPerson｜处理阶段 03
                // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
                // <<< MAIN-DEFENSE
                if (!status.ok() && firstFailure.ok())
                    firstFailure = std::move(status);
                finished = true;
                loop.quit();
            });
            // >>> MAIN-DEFENSE | refreshPerson｜处理阶段 04
            // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
            // <<< MAIN-DEFENSE
            if (!finished)
                loop.exec();
        }
        if (!page.value->nextOffset) {
            // >>> MAIN-DEFENSE | refreshPerson｜处理阶段 05
            // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
            // <<< MAIN-DEFENSE
            if (!hasFeeds)
                return {warehouse_contract::ErrorCode::NotFound, QStringLiteral("没有为该人物登记 RSS 或 Atom。")};
            return firstFailure;
        }
        request.offset = *page.value->nextOffset;
    } while (true);
}
// >>> MAIN-DEFENSE | 函数入口｜refreshDeepSeek
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：care_contract::Status refreshDeepSeek(QSqlDatabase& db, const QString& personId,
//                                        const QString& approvedSite, const QString& identityClue)

// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
care_contract::Status refreshDeepSeek(QSqlDatabase& db, const QString& personId,
                                     const QString& approvedSite, const QString& identityClue)
{
    using warehouse_contract::ErrorCode;
    const auto id = care_contract::validatePersonId(personId);
    // >>> MAIN-DEFENSE | refreshDeepSeek｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!id.ok())
        return id;
    const auto key = qgetenv("MEDIA_SERVER_DEEPSEEK_API_KEY").trimmed();
    if (key.isEmpty() || key.size() > 4096 || key.contains('\n') || key.contains('\r') || key.contains('\0'))
        // >>> MAIN-DEFENSE | refreshDeepSeek｜处理阶段 02
        // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
        // <<< MAIN-DEFENSE
        return {ErrorCode::InvalidArgument, QStringLiteral("请在服务器进程设置 MEDIA_SERVER_DEEPSEEK_API_KEY。")};
    QSqlQuery row(db);
    row.prepare("SELECT display_name FROM person_catalog WHERE person_id=?");
    row.addBindValue(personId);
    // >>> MAIN-DEFENSE | refreshDeepSeek｜处理阶段 03
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!row.exec())
        return {ErrorCode::ServerError, QStringLiteral("无法读取可信人物目录。")};
    if (!row.next())
        return {ErrorCode::NotFound, QStringLiteral("人物未导入可信目录。")};
    const deepseek_person_news::Request input{personId, row.value(0).toString(), approvedSite, identityClue};
    row.finish();
    const auto source = deepseek_person_news::cacheSource(input);
    // >>> MAIN-DEFENSE | refreshDeepSeek｜处理阶段 04
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!source.status.ok())
        return source.status;
    if (!source.value)
        return {ErrorCode::ServerError, QStringLiteral("无法建立 DeepSeek 缓存来源。")};
    const auto registered = person_source_config::registerBatch(db, {
        QStringLiteral("Publisher and identity reviewed by server operator."),
        {{personId, *source.value}}
    });
    // >>> MAIN-DEFENSE | refreshDeepSeek｜处理阶段 05
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
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
    // >>> MAIN-DEFENSE | refreshDeepSeek｜处理阶段 06
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!result)
        loop.exec();
    return result.value_or(care_contract::Status{ErrorCode::ServerError, QStringLiteral("资讯刷新已中止。")});
}
} // namespace

// >>> MAIN-DEFENSE | 函数入口｜main
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：int main(int argc, char** argv) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
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
        // >>> MAIN-DEFENSE | main｜处理阶段 01
        // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
        // <<< MAIN-DEFENSE
        return 1;
    };
    const int selected = int(parser.isSet(config)) + int(parser.isSet(refresh)) + int(parser.isSet(deepseek));
    if (parser.value(database).trimmed().isEmpty() || selected != 1)
        // >>> MAIN-DEFENSE | main｜处理阶段 02
        // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
        // <<< MAIN-DEFENSE
        return fail("Specify --db and exactly one of --config, --refresh-person or --deepseek-person.");
    if ((parser.isSet(deepseek) && (!parser.isSet(site) || !parser.isSet(clue))) ||
        (!parser.isSet(deepseek) && (parser.isSet(site) || parser.isSet(clue))))
        return fail("--deepseek-person requires both --news-site and --news-clue.");
    // >>> MAIN-DEFENSE | main｜处理阶段 03
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!QFileInfo(parser.value(database)).isFile())
        return fail("Server database does not exist; initialize it with media_server first.");
    int code = 0;
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", "person-source-operator");
        db.setDatabaseName(QFileInfo(parser.value(database)).absoluteFilePath());
        db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=5000");
        // >>> MAIN-DEFENSE | main｜处理阶段 04
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!db.open()) {
            code = fail("Cannot open server database.");
        } else {
            QSqlQuery query(db);
            // >>> MAIN-DEFENSE | main｜处理阶段 05
            // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
            // <<< MAIN-DEFENSE
            if (!query.exec("PRAGMA foreign_keys=ON")) {
                code = fail("Cannot enable foreign key checks.");
            } else if (parser.isSet(deepseek)) {
                const auto status = refreshDeepSeek(db, parser.value(deepseek), parser.value(site), parser.value(clue));
                // >>> MAIN-DEFENSE | main｜处理阶段 06
                // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
                // <<< MAIN-DEFENSE
                if (!status.ok()) code = fail(status.message);
                else QTextStream(stdout) << "DeepSeek person news refreshed." << Qt::endl;
            } else if (parser.isSet(refresh)) {
                const auto status = refreshPerson(db, parser.value(refresh));
                // >>> MAIN-DEFENSE | main｜处理阶段 07
                // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
                // <<< MAIN-DEFENSE
                if (!status.ok()) code = fail(status.message);
                else QTextStream(stdout) << "Person feeds refreshed." << Qt::endl;
            } else {
                const auto result = person_source_config::registerFile(db, parser.value(config));
                // >>> MAIN-DEFENSE | main｜处理阶段 08
                // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
                // <<< MAIN-DEFENSE
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
    // >>> MAIN-DEFENSE | main｜处理阶段 09
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return code;
}
