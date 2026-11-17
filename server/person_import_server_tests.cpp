#include "online_care_client.h"
#include "warehouse_server.h"
#include "person_credit_import.h"
#include "person_store_sqlite.h"
#include "person_feed_refresh.h"
#include "deepseek_person_news.h"
#include "person_source_config.h"
#include "care_person.h"
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <cstring>
#include <QProcess>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

using namespace care_contract;
using warehouse_contract::WarehouseSummary;
using warehouse_contract::WarehouseItem;
using warehouse_contract::Visibility;
using warehouse_contract::ErrorCode;

namespace {
class FeedReply final : public QNetworkReply {
public:
    FeedReply(const QNetworkRequest& request, int status, QByteArray content, QObject* parent)
        : QNetworkReply(parent), content_(std::move(content))
    {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        QTimer::singleShot(0, this, [this] {
            if (isFinished()) return;
            emit readyRead();
            if (!isFinished()) {
                setFinished(true);
                emit finished();
            }
        });
    }
    void abort() override
    {
        if (isFinished()) return;
        setError(QNetworkReply::OperationCanceledError, QStringLiteral("Aborted"));
        setFinished(true);
        emit finished();
    }
    qint64 bytesAvailable() const override { return content_.size() + QNetworkReply::bytesAvailable(); }
protected:
    qint64 readData(char* buffer, qint64 maxSize) override
    {
        if (content_.isEmpty()) return -1;
        const auto count = qMin(maxSize, qint64(content_.size()));
        std::memcpy(buffer, content_.constData(), size_t(count));
        content_.remove(0, count);
        return count;
    }
private:
    QByteArray content_;
};

class FeedNetwork final : public QNetworkAccessManager {
public:
    int status = 200;
    QByteArray content;
    QUrl requestedUrl;
protected:
    QNetworkReply* createRequest(Operation, const QNetworkRequest& request, QIODevice*) override
    {
        requestedUrl = request.url();
        return new FeedReply(request, status, content, this);
    }
};

class NewsNetwork final : public QNetworkAccessManager {
public:
    QList<QNetworkRequest> requests;
    QList<QJsonObject> payloads;
    int searchStatus = 200;
    int fetchStatus = 200;
    QJsonObject searchBody;
    QJsonObject fetchBody;
    QJsonObject unrelatedBody;
protected:
    QNetworkReply* createRequest(Operation, const QNetworkRequest& request, QIODevice* data) override
    {
        requests.append(request);
        payloads.append(data ? QJsonDocument::fromJson(data->readAll()).object() : QJsonObject{});
        const bool search = request.url().path() == "/v1/tools/search";
        const bool unrelated = payloads.last().value("url").toString().endsWith("/unrelated");
        const auto reply = search ? searchBody : unrelated ? unrelatedBody : fetchBody;
        return new FeedReply(request, search ? searchStatus : fetchStatus,
            QJsonDocument(reply).toJson(QJsonDocument::Compact), this);
    }
};
} // namespace

class PersonImportServerTests : public QObject {
    Q_OBJECT
private slots:
    void webPersonSearchFetchesOnlyApprovedProfiles()
    {
        NewsNetwork network;
        network.searchBody = {{"results", QJsonArray{
            QJsonObject{{"url", "https://example.invalid/people/123"}},
            QJsonObject{{"url", "https://catalog.afi.com/Film/56615-THE-FOUR-SEASONS"}},
            QJsonObject{{"url", "https://catalog.afi.com/Catalog/PersonDetails/8664?from=search"}},
        }}};
        network.fetchBody = {{"title", "Alan Alda"}, {"content", "# Alan Alda\nFilmography"}};
        bool done = false;
        Result<QList<PersonWebSource>> result;
        const auto lookup = [&] {
            done = false;
            deepseek_person_news::findPeople(network, "Alan Alda", "test-key", [&](auto value) {
                result = std::move(value);
                done = true;
            });
        };
        lookup();
        QTRY_VERIFY(done);
        QVERIFY(result.status.ok() && result.value);
        QCOMPARE(result.value->size(), 1);
        QCOMPARE(result.value->front().title, QString("Alan Alda"));
        QCOMPARE(network.requests.size(), 2);
        QCOMPARE(network.requests.at(0).url().path(), QString("/v1/tools/search"));
        QCOMPARE(network.requests.at(1).url().path(), QString("/v1/tools/fetch"));
        QCOMPARE(network.payloads.at(1).value("url").toString(),
                 QString("https://catalog.afi.com/Catalog/PersonDetails/8664"));

        network.fetchBody = {{"title", "Another person"}, {"content", "# Another person"}};
        lookup();
        QTRY_VERIFY(done);
        QVERIFY(result.status.ok() && result.value && result.value->isEmpty());

        network.fetchStatus = 503;
        lookup();
        QTRY_VERIFY(done);
        QVERIFY(!result.status.ok());
    }

    void importedPeopleReachResourceHistoryFootprintAndFollowRecommendation() {
        QTemporaryDir dir;
        const auto path = dir.filePath("server.sqlite");
        ResourceRef ref;
        qint64 aliceId = 0;
        QString error;
        {
            WarehouseServer server(llm_recommendation::Config{});
            QVERIFY2(server.openDatabase(path,error), qPrintable(error));
            QVERIFY(server.addUser("alice","password-123",error));
            QVERIFY(server.addUser("bob","password-123",error));
            QVERIFY(server.listen(QHostAddress::LocalHost));
            OnlineCareClient alice,bob;
            bool done = false;
            Status status;
            for (auto* client : {&alice,&bob}) {
                client->connectToServer("127.0.0.1",server.serverPort(),[&](Status s){status=s;done=true;});
                QTRY_VERIFY(done); QVERIFY(status.ok()); done=false;
                client->login(client==&alice?"alice":"bob","password-123",[&](auto r){
                    status=r.status; if(client==&alice && r.value) aliceId=r.value->userId;done=true;
                });
                QTRY_VERIFY(done); QVERIFY(status.ok()); done=false;
            }
            Result<WarehouseSummary> warehouse;
            alice.createWarehouse("Private demo",Visibility::Private,[&](auto r){warehouse=r;done=true;});
            QTRY_VERIFY(done); QVERIFY(warehouse.value);done=false;
            MediaRecord media;media.id="local-row";media.fileName="Fictional performance.mp4";
            media.fileType="MP4";media.category="music";media.actors={"Unconfirmed CSV name"};
            media.releaseDate=QDate(2024,1,1);
            Result<WarehouseItem> item;
            alice.addItem(warehouse.value->warehouseId,media,[&](auto r){item=r;done=true;});
            QTRY_VERIFY(done);QVERIFY(item.value);done=false;
            Result<ResourceSummary> work;
            alice.getResource({warehouse.value->warehouseId,item.value->itemId},[&](auto r){work=r;done=true;});
            QTRY_VERIFY(done);QVERIFY(work.value);QVERIFY(work.value->people.isEmpty());done=false;
            ref=work.value->resource;
            {
                auto db=QSqlDatabase::addDatabase("QSQLITE","person-import-live-test");
                db.setDatabaseName(path);QVERIFY(db.open());
                QSqlQuery q(db);QVERIFY(q.exec("PRAGMA foreign_keys=ON"));
                person_credit_import::Batch batch;
                batch.synthetic=true;batch.provenance="Hand-authored fictional integration fixture.";
                batch.people={{"person:demo","Fictional Person",QUrl("https://example.invalid/person/demo"),{"Demo alias"}}};
                batch.credits={{ref,"person:demo",PersonRole::Actor,QUrl("https://example.invalid/credit/demo")},
                               {ref,"person:demo",PersonRole::Musician,QUrl("https://example.invalid/credit/demo")}};
                auto imported=person_credit_import::importBatch(db,batch,{true});
                QVERIFY2(imported.status.ok(),qPrintable(imported.status.message));QVERIFY(imported.value);
                QCOMPARE(imported.value->peopleInserted,1);QCOMPARE(imported.value->creditsInserted,2);
                const auto again=person_credit_import::importBatch(db,batch,{true});
                QVERIFY(again.status.ok() && again.value);QCOMPARE(again.value->creditsInserted,0);
                db.close();
            }
            QSqlDatabase::removeDatabase("person-import-live-test");

            Result<Page<PersonSummary>> found;
            bob.searchPeople({"Demo alias", {}}, [&](auto r){found=r;done=true;});
            QTRY_VERIFY(done); QVERIFY(found.value && found.value->items.isEmpty()); done=false;
            alice.searchPeople({QStringLiteral("Ｄｅｍｏ alias"), {}}, [&](auto r){found=r;done=true;});
            QTRY_VERIFY(done); QVERIFY(found.status.ok() && found.value); done=false;
            QCOMPARE(found.value->items.size(), 1);
            QCOMPARE(found.value->items.first().personId, QStringLiteral("person:demo"));
            QCOMPARE(found.value->items.first().roles.size(),2);
            QVERIFY(!found.value->items.first().followed);

            Result<Page<ResourceSummary>> linked;
            bob.getPersonWorks("person:demo", {}, [&](auto r){linked=r;done=true;});
            QTRY_VERIFY(done); QCOMPARE(linked.status.code, ErrorCode::NotFound); done=false;
            alice.getPersonWorks("person:demo", {}, [&](auto r){linked=r;done=true;});
            QTRY_VERIFY(done); QVERIFY(linked.status.ok() && linked.value); done=false;
            QCOMPARE(linked.value->items.size(), 1);
            QCOMPARE(linked.value->items.first().resource.source.itemId, ref.source.itemId);

            bob.setFollow("person:demo", true, [&](Status s){status=s;done=true;});
            QTRY_VERIFY(done); QCOMPARE(status.code,ErrorCode::PermissionDenied); done=false;
            alice.setFollow("person:demo", true, [&](Status s){status=s;done=true;});
            QTRY_VERIFY(done); QVERIFY(status.ok()); done=false;
            bob.listFollows({}, [&](auto r){found=r;done=true;});
            QTRY_VERIFY(done); QVERIFY(found.value && found.value->items.isEmpty()); done=false;
            alice.listFollows({}, [&](auto r){found=r;done=true;});
            QTRY_VERIFY(done); QVERIFY(found.value && found.value->items.size()==1); done=false;

            const auto now = QDateTime::currentSecsSinceEpoch();
            const PersonUpdatesRequest updates{"person:demo", {now-3600,now+86400}, {}};
            Result<Page<PersonUpdate>> news;
            alice.getPersonUpdates(updates, [&](auto r){news=r;done=true;});
            QTRY_VERIFY(done); QCOMPARE(news.status.code,ErrorCode::ServerError); done=false;
            QVERIFY(news.status.message.contains(QStringLiteral("尚未配置")));
            {
                auto db=QSqlDatabase::addDatabase("QSQLITE","person-import-updates-test");
                db.setDatabaseName(path); QVERIFY(db.open());
                QSqlQuery q(db); QVERIFY(q.exec("PRAGMA foreign_keys=ON"));
                const QUrl feed("https://example.invalid/feed");
                const person_source_config::Batch config{"Fictional test feed", {{"person:demo", feed}}};
                const auto saved = person_source_config::registerBatch(db, config);
                QVERIFY(saved.status.ok() && saved.value);
                const auto sources = person_source_config::listSourcesForPerson(db, "person:demo", {});
                QVERIFY(sources.value && sources.value->items.size() == 1);
                person_store::SqliteStore store(db);
                FeedNetwork network;
                network.content = R"(<rss version="2.0"><channel>
                    <title>Example</title><item><title>Fictional story</title>
                    <link>https://publisher.example.org/story</link></item>
                    </channel></rss>)";
                person_feed_refresh::refresh(store, network, sources.value->items.first(),
                    [&](Status outcome) { status = std::move(outcome); done = true; });
                QTRY_VERIFY(done); QVERIFY2(status.ok(), qPrintable(status.message)); done = false;
                QCOMPARE(network.requestedUrl, feed);
                db.close();
            }
            QSqlDatabase::removeDatabase("person-import-updates-test");
            bob.getPersonUpdates(updates, [&](auto r){news=r;done=true;});
            QTRY_VERIFY(done); QCOMPARE(news.status.code,ErrorCode::PermissionDenied); done=false;
            alice.getPersonUpdates(updates, [&](auto r){news=r;done=true;});
            QTRY_VERIFY(done); QVERIFY(news.status.ok() && news.value); done=false;
            QCOMPARE(news.value->items.size(),1);
            QCOMPARE(news.value->items.first().title,QStringLiteral("Fictional story"));
            QVERIFY(!news.value->items.first().publishedAtUtc);
            QVERIFY(!news.value->items.first().stale);
            QCOMPARE(news.value->items.first().sourceUrl, QUrl("https://publisher.example.org/story"));
            {
                auto db = QSqlDatabase::addDatabase("QSQLITE", "person-import-failed-refresh");
                db.setDatabaseName(path); QVERIFY(db.open());
                const auto sources = person_source_config::listSourcesForPerson(db, "person:demo", {});
                QVERIFY(sources.value && sources.value->items.size() == 1);
                person_store::SqliteStore store(db);
                FeedNetwork network;
                network.content = "<feed xmlns=\"http://www.w3.org/2005/Atom\">"
                                  "<title>Example</title><entry><title>Fictional story</title>"
                                  "<link rel=\"alternate\" href=\"https://publisher.example.org/story\"/></entry></feed>";
                const auto refresh = [&] {
                    person_feed_refresh::refresh(store, network, sources.value->items.first(),
                        [&](Status outcome) { status = std::move(outcome); done = true; });
                };
                refresh();
                QTRY_VERIFY(done); QVERIFY2(status.ok(), qPrintable(status.message)); done = false;
                network.status = 503;
                refresh();
                QTRY_VERIFY(done); QCOMPARE(status.code, ErrorCode::ServerError); done = false;
                network.status = 200;
                network.content = "<html>Unexpected response</html>";
                refresh();
                QTRY_VERIFY(done); QCOMPARE(status.code, ErrorCode::ServerError); done = false;
            }
            QSqlDatabase::removeDatabase("person-import-failed-refresh");
            alice.getPersonUpdates(updates, [&](auto r){news=r;done=true;});
            QTRY_VERIFY(done); QVERIFY(news.status.ok() && news.value); done = false;
            QCOMPARE(news.value->items.size(), 1);
            QCOMPARE(news.value->items.first().title, QStringLiteral("Fictional story"));
            QVERIFY(news.value->items.first().stale);

            const QString approvedArticle = QStringLiteral("https://publisher.example.org/story");
            {
                auto db = QSqlDatabase::addDatabase("QSQLITE", "person-import-deepseek-news");
                db.setDatabaseName(path); QVERIFY(db.open());
                const deepseek_person_news::Request input{
                    "person:demo", "Fictional Person", "publisher.example.org", "Fictional performance"
                };
                const auto source = deepseek_person_news::cacheSource(input);
                QVERIFY(source.status.ok() && source.value);
                QVERIFY(deepseek_person_news::isSearchSource(*source.value));
                QVERIFY(!deepseek_person_news::isSearchSource(QUrl("https://example.invalid/feed")));
                QVERIFY(person_source_config::registerBatch(db, {"Test provider", {{"person:demo", *source.value}}}).status.ok());
                person_store::SqliteStore store(db);
                NewsNetwork network;
                network.searchBody = {{"results", QJsonArray{
                    QJsonObject{{"url", "https://unapproved.example.org/story"}},
                    QJsonObject{{"url", approvedArticle}, {"date", "2099-11-15T01:00:00Z"}},
                    QJsonObject{{"url", approvedArticle}},
                    QJsonObject{{"url", "https://publisher.example.org/unrelated"}}
                }}};
                network.fetchBody = {{"title", "Publisher story"}, {"content", "Fictional Person premieres Fictional performance."}};
                network.unrelatedBody = {{"title", "Unrelated article"}, {"content", "Other people and other works."}};
                const auto refresh = [&](const QByteArray& key) {
                    deepseek_person_news::refresh(store, network, input, key,
                        [&](Status outcome) { status = std::move(outcome); done = true; });
                };
                refresh({});
                QVERIFY(done); QCOMPARE(status.code, ErrorCode::InvalidArgument); done = false;
                QVERIFY(network.requests.isEmpty());
                refresh("fixture-key");
                QTRY_VERIFY(done); QVERIFY2(status.ok(), qPrintable(status.message)); done = false;
                QCOMPARE(network.requests.size(), 3);
                QCOMPARE(network.requests.first().url(), QUrl("https://api.deepseek.com/v1/tools/search"));
                QCOMPARE(network.requests.at(1).url(), QUrl("https://api.deepseek.com/v1/tools/fetch"));
                QCOMPARE(network.payloads.first().value("query").toString(),
                         QString::fromLatin1("\"Fictional Person\" \"Fictional performance\" site:publisher.example.org"));
                QCOMPARE(network.payloads.at(1).value("url").toString(), approvedArticle);
                QCOMPARE(network.payloads.at(2).value("url").toString(),
                         QStringLiteral("https://publisher.example.org/unrelated"));
                // Force two successful providers into the same UTC second.
                QSqlQuery alignTimes(db);
                alignTimes.prepare("UPDATE person_updates SET fetched_at_utc=? WHERE source_url=?");
                alignTimes.addBindValue(now);
                alignTimes.addBindValue(approvedArticle);
                QVERIFY(alignTimes.exec());
                QCOMPARE(alignTimes.numRowsAffected(), qint64(2));
                alice.getPersonUpdates(updates, [&](auto r){news=r;done=true;});
                QTRY_VERIFY(done); QVERIFY2(news.status.ok() && news.value, qPrintable(news.status.message)); done = false;
                const auto deepseekArticle = [&]() -> const PersonUpdate* {
                    if (!news.value) return nullptr;
                    for (const auto& article : news.value->items)
                        if (article.sourceUrl == QUrl(approvedArticle) && article.title == "Publisher story")
                            return &article;
                    return nullptr;
                };
                QCOMPARE(news.value->items.size(), 2);
                QVERIFY(deepseekArticle());
                QVERIFY(!deepseekArticle()->stale && !deepseekArticle()->publishedAtUtc && !deepseekArticle()->eventAtUtc);
                network.fetchStatus = 503;
                refresh("fixture-key");
                QTRY_VERIFY(done); QCOMPARE(status.code, ErrorCode::ServerError); done = false;
                alice.getPersonUpdates(updates, [&](auto r){news=r;done=true;});
                QTRY_VERIFY(done); QVERIFY(news.status.ok() && news.value); done = false;
                QVERIFY(deepseekArticle() && deepseekArticle()->stale);

                network.fetchStatus = 200;
                network.fetchBody = {{"title", "Another person"}, {"content", "Fictional Person is in a different film."}};
                refresh("fixture-key");
                QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
                alice.getPersonUpdates(updates, [&](auto r){news=r;done=true;});
                QTRY_VERIFY(done); QVERIFY(news.status.ok() && news.value); done = false;
                QVERIFY(!deepseekArticle());
                QCOMPARE(news.value->items.size(), 1); // Refresh never replaces the RSS partition.
            }
            QSqlDatabase::removeDatabase("person-import-deepseek-news");

            Result<WarehouseSummary> publicWarehouse;
            alice.createWarehouse("Public demo", Visibility::Public, [&](auto r){publicWarehouse=r;done=true;});
            QTRY_VERIFY(done); QVERIFY(publicWarehouse.status.ok() && publicWarehouse.value); done = false;
            const auto publicId = publicWarehouse.value->warehouseId;
            Result<WarehouseItem> publicItem;
            alice.addItem(publicId, media, [&](auto r){publicItem=r;done=true;});
            QTRY_VERIFY(done); QVERIFY(publicItem.status.ok() && publicItem.value); done = false;
            Result<warehouse_contract::WarehouseDetail> publicRead;
            bob.getWarehouse(publicId, [&](auto r){publicRead=r;done=true;});
            QTRY_VERIFY(done); QVERIFY(publicRead.status.ok() && publicRead.value); done = false;
            QCOMPARE(publicRead.value->items.size(), 1);
            bob.addItem(publicId, media, [&](auto r){status=r.status;done=true;});
            QTRY_VERIFY(done); QCOMPARE(status.code, ErrorCode::PermissionDenied); done = false;
            bob.removeItem(publicId, publicItem.value->itemId, [&](Status s){status=s;done=true;});
            QTRY_VERIFY(done); QCOMPARE(status.code, ErrorCode::PermissionDenied); done = false;
            alice.removeItem(publicId, publicItem.value->itemId, [&](Status s){status=s;done=true;});
            QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;

            bob.getResource(ref.source,[&](auto r){work=r;done=true;});
            QTRY_VERIFY(done);QCOMPARE(work.status.code,ErrorCode::PermissionDenied);done=false;
            alice.getResource(ref.source,[&](auto r){work=r;done=true;});
            QTRY_VERIFY(done);QVERIFY(work.value);QCOMPARE(work.value->people.size(),1);
            QCOMPARE(work.value->people.first().roles.size(),2);QVERIFY(work.value->people.first().followed);done=false;
            care_algorithms::CatalogSnapshot catalog;
            catalog.readableWorks={*work.value};catalog.followedPeople={"person:demo"};
            const auto people=care_algorithms::searchPeople({"Fictional",{}},catalog);
            QVERIFY(people.status.ok() && people.value);QCOMPARE(people.value->items.size(),1);
            const auto works=care_algorithms::worksForPerson("person:demo",{},catalog);
            QVERIFY(works.status.ok() && works.value);QCOMPARE(works.value->items.size(),1);
            Result<TemporalRecommendationResult> recommendation;
            alice.recommendTemporal({},[&](auto r){recommendation=r;done=true;});
            QTRY_VERIFY(done);QVERIFY(recommendation.status.ok() && recommendation.value);done=false;
            QCOMPARE(recommendation.value->items.size(),1);
            QVERIFY(recommendation.value->items.first().reason.contains(QString::fromUtf8("主动关注=0.050000")));
            alice.recordBehavior({"view:imported",BehaviorAction::View,ref,{}},[&](Status s){status=s;done=true;});
            QTRY_VERIFY(done);QVERIFY(status.ok());done=false;
            const auto history=server.queryBehavior(aliceId,{0,MaxUtcExclusive});
            QVERIFY(history);QCOMPARE(history->size(),1);QCOMPARE(history->first().peopleSnapshot.size(),1);
            alice.removeItem(ref.source.warehouseId,ref.source.itemId,[&](Status s){status=s;done=true;});
            QTRY_VERIFY(done);QVERIFY(status.ok()); done=false;
            alice.getPersonWorks("person:demo", {}, [&](auto r){linked=r;done=true;});
            QTRY_VERIFY(done); QCOMPARE(linked.status.code,ErrorCode::NotFound); done=false;
            alice.listFollows({}, [&](auto r){found=r;done=true;});
            QTRY_VERIFY(done); QVERIFY(found.value && found.value->items.size()==1); done=false;
            alice.setFollow("person:demo", false, [&](Status s){status=s;done=true;});
            QTRY_VERIFY(done); QVERIFY(status.ok()); done=false;
            alice.listFollows({}, [&](auto r){found=r;done=true;});
            QTRY_VERIFY(done); QVERIFY(found.value && found.value->items.isEmpty());
        }
        {
            WarehouseServer server(llm_recommendation::Config{});
            QVERIFY(server.openDatabase(path,error));QVERIFY(server.listen(QHostAddress::LocalHost));
            OnlineCareClient alice;bool done=false;Status status;
            alice.connectToServer("127.0.0.1",server.serverPort(),[&](Status s){status=s;done=true;});
            QTRY_VERIFY(done);QVERIFY(status.ok());done=false;
            alice.login("alice","password-123",[&](auto r){status=r.status;done=true;});
            QTRY_VERIFY(done);QVERIFY(status.ok());done=false;
            Result<FootprintReport> report;const auto now=QDateTime::currentSecsSinceEpoch();
            alice.getFootprint({{now-86400,now+2},{}},[&](auto r){report=r;done=true;});
            QTRY_VERIFY(done);QVERIFY(report.status.ok() && report.value);
            QCOMPARE(report.value->current.views,1);QCOMPARE(report.value->current.people.size(),2);
            for (const auto& person:report.value->current.people) {QCOMPARE(person.personId,QString("person:demo"));QCOMPARE(person.count,1);}
        }
    }
    void cliRejectsMissingDatabaseAndDefaultsToRejectingSyntheticInput() {
        QTemporaryDir dir;QString error;
        const auto path=dir.filePath("operator.sqlite");
        {WarehouseServer server(llm_recommendation::Config{});QVERIFY(server.openDatabase(path,error));}
        const auto exe=QCoreApplication::applicationDirPath()+"/media_person_import"
#ifdef Q_OS_WIN
            +".exe"
#endif
            ;
        QVERIFY(QFile::exists(exe));
        const auto fixture=QString(PERSON_IMPORT_SOURCE_DIR)+"/sample-data/person/import-demo.synthetic.json";
        QProcess process;
        process.start(exe,{dir.filePath("missing.sqlite"),fixture});
        QVERIFY(process.waitForFinished());QVERIFY(process.exitCode()!=0);QVERIFY(!QFile::exists(dir.filePath("missing.sqlite")));
        process.start(exe,{path,fixture});QVERIFY(process.waitForFinished());QVERIFY(process.exitCode()!=0);
        QVERIFY(!process.readAllStandardError().isEmpty());
        {
            auto db=QSqlDatabase::addDatabase("QSQLITE","person-import-cli-seed");db.setDatabaseName(path);QVERIFY(db.open());
            QSqlQuery q(db);
            // Synthetic registered source fixture; the importer never creates these rows.
            QVERIFY(q.exec("INSERT INTO network_users(id,username,username_key,password_hash) VALUES(1,'fixture-user','fixture-user','unused')"));
            QVERIFY(q.exec("INSERT INTO warehouses(id,owner_id,name,is_public) VALUES(1,1,'Fixture',0)"));
            QVERIFY(q.exec("INSERT INTO warehouse_items(id,warehouse_id,media) VALUES(1,1,'{}')"));
            QVERIFY(q.exec("INSERT INTO work_sources(warehouse_id,item_id,work_id) VALUES(1,1,'srv:demo-registered-work')"));
            db.close();
        }
        QSqlDatabase::removeDatabase("person-import-cli-seed");
        process.start(exe,{"--allow-synthetic",path,fixture});QVERIFY(process.waitForFinished());
        QVERIFY2(process.exitCode()==0,process.readAllStandardError().constData());
        const auto report=QJsonDocument::fromJson(process.readAllStandardOutput()).object();
        QCOMPARE(report.value("people_inserted").toInt(),1);QCOMPARE(report.value("credits_inserted").toInt(),2);
        process.start(exe,{"--allow-synthetic",path,fixture});QVERIFY(process.waitForFinished());QCOMPARE(process.exitCode(),0);
        const auto repeated=QJsonDocument::fromJson(process.readAllStandardOutput()).object();
        QCOMPARE(repeated.value("people_inserted").toInt(),0);QCOMPARE(repeated.value("credits_inserted").toInt(),0);
        process.start(exe,{"--help"});QVERIFY(process.waitForFinished());QCOMPARE(process.exitCode(),0);
    }
};
QTEST_GUILESS_MAIN(PersonImportServerTests)
#include "person_import_server_tests.moc"
