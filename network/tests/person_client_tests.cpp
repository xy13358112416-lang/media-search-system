#include "online_care_client.h"
#include "media_json.h"
#include "person_json.h"
#include <QJsonArray>
#include <QPointer>
#include <QTcpServer>
#include <QtTest>
#include <memory>

using namespace care_contract;
using warehouse_contract::ErrorCode;

class PersonFakeServer : public QObject {
public:
    QTcpServer server;
    QList<QPair<QPointer<QTcpSocket>, QJsonObject>> requests;
    PersonFakeServer() {
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (auto* socket = server.nextPendingConnection()) {
                auto decoder = std::make_shared<warehouse_protocol::FrameDecoder>();
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, decoder] {
                    QList<QJsonObject> frames; QString error;
                    if (!decoder->feed(socket->readAll(), frames, error)) { socket->abort(); return; }
                    for (const auto& frame : frames) {
                        const auto type = frame.value("type").toString();
                        if (type == "login") {
                            const auto username = frame.value("payload").toObject().value("username").toString();
                            reply(socket, frame, {{"token", "token-" + username},
                                {"user", QJsonObject{{"user_id", username == "alice" ? "1" : "2"}, {"username", username}}}});
                        } else if (type == "logout") reply(socket, frame, {});
                        else requests.append({socket, frame});
                    }
                });
            }
        });
    }
    static void reply(QTcpSocket* socket, const QJsonObject& request, const QJsonObject& payload) {
        QString error;
        socket->write(warehouse_protocol::encodeFrame({{"type", request.value("type").toString() + "_resp"},
            {"req_id", request.value("req_id")}, {"payload", payload}}, error));
    }

    static void reject(QTcpSocket* socket, const QJsonObject& request, const QString& code) {
        QString error;
        socket->write(warehouse_protocol::encodeFrame({{"type", "error_resp"},
            {"req_id", request.value("req_id")}, {"code", code}, {"message", "test server rejection"}}, error));
    }
};

namespace {
QJsonObject releaseUpdate()
{
    return {{"person_id", "person:1"}, {"update_id", "update:release"}, {"title", "Upcoming release"},
            {"kind", "release"}, {"source_url", "https://example.com/releases/one"},
            {"fetched_at_utc", "1761926400"}, {"event_at_utc", "1764520000"}, {"stale", true}};
}

QJsonObject updatesResponse(QJsonArray items)
{
    return {{"page", QJsonObject{{"items", items}}}};
}

bool signIn(PersonFakeServer& fake, OnlineCareClient& client)
{
    std::optional<Status> connection;
    client.connectToServer("127.0.0.1", fake.server.serverPort(), [&](auto status) {
        connection = std::move(status);
    });
    if (!QTest::qWaitFor([&] { return connection.has_value(); }) || !connection->ok()) {
        client.disconnectFromServer();
        return false;
    }
    std::optional<warehouse_contract::Result<warehouse_contract::UserInfo>> login;
    client.login("alice", "password", [&](auto result) { login = std::move(result); });
    if (!QTest::qWaitFor([&] { return login.has_value(); }) || !login->status.ok()) {
        client.disconnectFromServer();
        return false;
    }
    return true;
}
} // namespace

class PersonClientTests : public QObject {
    Q_OBJECT
private slots:
    void matchesOutOfOrderPagesAndWorks() {
        PersonFakeServer fake; QVERIFY(fake.server.listen(QHostAddress::LocalHost));
        OnlineCareClient client; bool done = false; Status status;
        client.connectToServer("127.0.0.1", fake.server.serverPort(), [&](Status s) { status = s; done = true; });
        QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
        client.login("alice", "password", [&](auto r) { status = r.status; done = true; });
        QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
        Result<Page<PersonSummary>> search, follows; bool searchDone = false, followDone = false;
        client.searchPeople({"person", {0, 1}}, [&](auto r) { search = r; searchDone = true; });
        client.listFollows({0, 1}, [&](auto r) { follows = r; followDone = true; });
        QTRY_COMPARE(fake.requests.size(), 2);
        QCOMPARE(fake.requests[0].second.value("type").toString(), QStringLiteral("search_people"));
        QCOMPARE(fake.requests[0].second.value("payload").toObject(),
                 (QJsonObject{{"query", "person"}, {"offset", "0"}, {"limit", 1}}));
        QCOMPARE(fake.requests[1].second.value("type").toString(), QStringLiteral("list_follows"));
        QCOMPARE(fake.requests[1].second.value("payload").toObject(),
                 (QJsonObject{{"offset", "0"}, {"limit", 1}}));
        const QJsonObject person{{"person_id", "person:1"}, {"name", "Person"}, {"roles", QJsonArray{"actor", "director"}}, {"followed", true}};
        PersonFakeServer::reply(fake.requests[1].first, fake.requests[1].second, {{"page", QJsonObject{{"items", QJsonArray{person}}}}});
        PersonFakeServer::reply(fake.requests[0].first, fake.requests[0].second, {{"page", QJsonObject{{"items", QJsonArray{person}}, {"next_offset", "1"}}}});
        QTRY_VERIFY(searchDone && followDone); QVERIFY(search.status.ok() && search.value && follows.status.ok() && follows.value);
        QCOMPARE(search.value->items.first().roles.size(), 2); QVERIFY(search.value->nextOffset); QCOMPARE(*search.value->nextOffset, 1);
        QVERIFY(follows.value->items.first().followed); QVERIFY(!follows.value->nextOffset);
        MediaRecord media; media.id = "m1"; media.fileName = "Film";
        const QJsonObject resource{{"work_id", "work:1"}, {"source", QJsonObject{{"warehouse_id", "1"}, {"item_id", "2"}}}};
        const QJsonObject work{{"resource", resource}, {"media", media_json::toJson(media)}, {"people", QJsonArray{person}}};
        Result<Page<ResourceSummary>> works;
        client.getPersonWorks("person:1", {0, 10}, [&](auto r) { works = r; done = true; });
        QTRY_COMPARE(fake.requests.size(), 3);
        QCOMPARE(fake.requests[2].second.value("type").toString(), QStringLiteral("get_person_works"));
        QCOMPARE(fake.requests[2].second.value("payload").toObject(),
                 (QJsonObject{{"person_id", "person:1"}, {"offset", "0"}, {"limit", 10}}));
        PersonFakeServer::reply(fake.requests[2].first, fake.requests[2].second, {{"page", QJsonObject{{"items", QJsonArray{work}}}}});
        QTRY_VERIFY(done); QVERIFY(works.status.ok() && works.value); QCOMPARE(works.value->items.first().resource.workId, QStringLiteral("work:1"));
    }
    void updatesPreserveOptionalDatesAndMatchWireContract()
    {
        PersonFakeServer fake;
        QVERIFY(fake.server.listen(QHostAddress::LocalHost));
        std::optional<Result<Page<PersonUpdate>>> result;
        OnlineCareClient client;
        QVERIFY(signIn(fake, client));

        const PersonUpdatesRequest input{"person:1", {1761926400, 1764600000}, {9007199254740993LL, 2}};
        client.getPersonUpdates(input, [&](auto reply) { result = std::move(reply); });
        QTRY_COMPARE(fake.requests.size(), 1);
        const auto request = fake.requests.first().second;
        QCOMPARE(request.value("type").toString(), QStringLiteral("get_person_updates"));
        QCOMPARE(request.value("token").toString(), QStringLiteral("token-alice"));
        QCOMPARE(request.value("payload").toObject(), (QJsonObject{
            {"person_id", "person:1"}, {"offset", "9007199254740993"}, {"limit", 2},
            {"range", QJsonObject{{"begin_utc", "1761926400"}, {"end_utc", "1764600000"}}}
        }));

        auto news = releaseUpdate();
        news.insert("update_id", "update:news");
        news.insert("kind", "news");
        news.insert("published_at_utc", "1761926300");
        news.insert("event_at_utc", QJsonValue::Null);
        news.insert("stale", false);
        auto payload = updatesResponse({releaseUpdate(), news});
        auto page = payload.value("page").toObject();
        page.insert("next_offset", "9007199254740995");
        payload.insert("page", page);
        PersonFakeServer::reply(fake.requests.first().first, request, payload);

        QTRY_VERIFY(result && result->status.ok() && result->value);
        QCOMPARE(result->value->items.size(), 2);
        QVERIFY(result->value->nextOffset);
        QCOMPARE(*result->value->nextOffset, 9007199254740995LL);
        const auto& release = result->value->items[0];
        QCOMPARE(release.kind, UpdateKind::Release);
        QCOMPARE(release.sourceUrl, QUrl("https://example.com/releases/one"));
        QVERIFY(!release.publishedAtUtc && release.eventAtUtc && release.stale);
        QCOMPARE(*release.eventAtUtc, 1764520000LL);
        const auto& article = result->value->items[1];
        QCOMPARE(article.kind, UpdateKind::News);
        QVERIFY(article.publishedAtUtc && !article.eventAtUtc && !article.stale);
        const auto encoded = person_json::encode(*result->value);
        const auto encodedItems = encoded.value("items").toArray();
        QVERIFY(!encodedItems[0].toObject().contains("published_at_utc"));
        QVERIFY(!encodedItems[1].toObject().contains("event_at_utc"));
        QCOMPARE(encoded.value("next_offset").toString(), QStringLiteral("9007199254740995"));
        Page<PersonUpdate> decoded;
        QVERIFY(person_json::decode(encoded, input.page, decoded));
        QCOMPARE(decoded.items[1].publishedAtUtc.value_or(-1), 1761926300LL);
    }

    void pageRequestCodecRejectsLossyOrOutOfBoundsValues()
    {
        PageRequest decoded;
        QVERIFY(person_json::decodePageRequest({{"offset", "9007199254740993"}, {"limit", 50}}, decoded));
        QCOMPARE(decoded.offset, 9007199254740993LL);
        QCOMPARE(decoded.limit, 50);
        QVERIFY(!person_json::decodePageRequest({{"offset", 2}, {"limit", 20}}, decoded));
        QVERIFY(!person_json::decodePageRequest({{"offset", "2"}, {"limit", 1.5}}, decoded));
        QVERIFY(!person_json::decodePageRequest({{"offset", "2"}, {"limit", 51}}, decoded));
        QVERIFY(!person_json::decodePageRequest({{"offset", "9223372036854775808"}, {"limit", 20}}, decoded));
    }

    void updatesRejectMalformedPayload_data()
    {
        QTest::addColumn<QString>("key");
        QTest::addColumn<QJsonValue>("value");
        QTest::addColumn<bool>("pageField");
        QTest::newRow("http") << QString("source_url") << QJsonValue("http://example.com") << false;
        QTest::newRow("missing-host") << QString("source_url") << QJsonValue("https:///news") << false;
        QTest::newRow("credentials") << QString("source_url") << QJsonValue("https://user:pass@example.com/news") << false;
        QTest::newRow("invalid-url") << QString("source_url") << QJsonValue("https://example.com/bad path") << false;
        QTest::newRow("wrong-person") << QString("person_id") << QJsonValue("person:other") << false;
        QTest::newRow("bad-update-id") << QString("update_id") << QJsonValue("bad id") << false;
        QTest::newRow("unknown-kind") << QString("kind") << QJsonValue("gossip") << false;
        QTest::newRow("wrong-stale-type") << QString("stale") << QJsonValue("true") << false;
        QTest::newRow("numeric-date") << QString("event_at_utc") << QJsonValue(75) << false;
        QTest::newRow("negative-date") << QString("published_at_utc") << QJsonValue("-1") << false;
        QTest::newRow("missing-fetch-date") << QString("fetched_at_utc") << QJsonValue(QJsonValue::Null) << false;
        QTest::newRow("date-out-of-range") << QString("event_at_utc") << QJsonValue("253402300800") << false;
        QTest::newRow("nonadvancing-page") << QString("next_offset") << QJsonValue("3") << true;
        QTest::newRow("oversized-page") << QString("items") << QJsonValue(QJsonArray{releaseUpdate(), releaseUpdate()}) << true;
    }

    void updatesRejectMalformedPayload()
    {
        QFETCH(QString, key);
        QFETCH(QJsonValue, value);
        QFETCH(bool, pageField);
        PersonFakeServer fake;
        QVERIFY(fake.server.listen(QHostAddress::LocalHost));
        std::optional<Result<Page<PersonUpdate>>> result;
        OnlineCareClient client;
        QVERIFY(signIn(fake, client));
        client.getPersonUpdates({"person:1", {1, 1764600000}, {3, 1}}, [&](auto reply) { result = std::move(reply); });
        QTRY_COMPARE(fake.requests.size(), 1);
        auto item = releaseUpdate();
        if (!pageField)
            item.insert(key, value);
        QJsonObject page{{"items", QJsonArray{item}}};
        if (pageField)
            page.insert(key, value);
        PersonFakeServer::reply(fake.requests.first().first, fake.requests.first().second, {{"page", page}});
        QTRY_VERIFY(result);
        QCOMPARE(result->status.code, ErrorCode::ProtocolError);
        QVERIFY(!result->value);
    }

    void updatesDistinguishEmptyPageFromServerError()
    {
        PersonFakeServer fake;
        QVERIFY(fake.server.listen(QHostAddress::LocalHost));
        std::optional<Result<Page<PersonUpdate>>> result;
        OnlineCareClient client;
        QVERIFY(signIn(fake, client));
        const PersonUpdatesRequest input{"person:1", {1, 1764600000}, {}};
        const auto done = [&](auto reply) { result = std::move(reply); };
        client.getPersonUpdates(input, done);
        QTRY_COMPARE(fake.requests.size(), 1);
        PersonFakeServer::reply(fake.requests[0].first, fake.requests[0].second, updatesResponse({}));
        QTRY_VERIFY(result && result->status.ok() && result->value);
        QVERIFY(result->value->items.isEmpty());
        result.reset();
        client.getPersonUpdates(input, done);
        QTRY_COMPARE(fake.requests.size(), 2);
        PersonFakeServer::reject(fake.requests[1].first, fake.requests[1].second, "PermissionDenied");
        QTRY_VERIFY(result);
        QCOMPARE(result->status.code, ErrorCode::PermissionDenied);
        QVERIFY(!result->value);
    }

    void ignoresOldUpdatesOnAccountChange()
    {
        PersonFakeServer fake;
        QVERIFY(fake.server.listen(QHostAddress::LocalHost));
        int oldCalls = 0;
        ErrorCode oldCode = ErrorCode::None;
        bool switched = false;
        std::optional<Result<Page<PersonUpdate>>> current;
        OnlineCareClient client;
        QVERIFY(signIn(fake, client));
        const PersonUpdatesRequest input{"person:1", {1, 1764600000}, {}};
        client.getPersonUpdates(input, [&](auto reply) { ++oldCalls; oldCode = reply.status.code; });
        QTRY_COMPARE(fake.requests.size(), 1);
        client.login("bob", "password", [&](auto login) { switched = login.status.ok() && login.value->username == "bob"; });
        QTRY_VERIFY(switched);
        QCOMPARE(oldCalls, 1);
        QCOMPARE(oldCode, ErrorCode::Cancelled);
        client.getPersonUpdates(input, [&](auto reply) { current = std::move(reply); });
        QTRY_COMPARE(fake.requests.size(), 2);
        QCOMPARE(fake.requests[1].second.value("token").toString(), QStringLiteral("token-bob"));
        PersonFakeServer::reply(fake.requests[0].first, fake.requests[0].second, updatesResponse({releaseUpdate()}));
        PersonFakeServer::reply(fake.requests[1].first, fake.requests[1].second, updatesResponse({}));
        QTRY_VERIFY(current && current->status.ok() && current->value);
        QVERIFY(current->value->items.isEmpty());
        QCOMPARE(oldCalls, 1);
    }

    void updateReadAndFollowWriteCompleteOnceOnTimeoutOrDisconnect_data()
    {
        QTest::addColumn<bool>("timeout");
        QTest::newRow("timeout") << true;
        QTest::newRow("server-disconnect") << false;
    }

    void updateReadAndFollowWriteCompleteOnceOnTimeoutOrDisconnect()
    {
        QFETCH(bool, timeout);
        PersonFakeServer fake;
        QVERIFY(fake.server.listen(QHostAddress::LocalHost));
        int reads = 0;
        int writes = 0;
        ErrorCode readCode = ErrorCode::None;
        ErrorCode writeCode = ErrorCode::None;
        OnlineCareClient client;
        QVERIFY(signIn(fake, client));
        client.getPersonUpdates({"person:1", {1, 1764600000}, {}},
                                [&](auto reply) { ++reads; readCode = reply.status.code; });
        client.setFollow("person:1", true, [&](auto status) { ++writes; writeCode = status.code; });
        QTRY_COMPARE(fake.requests.size(), 2);
        QCOMPARE(fake.requests[1].second.value("payload").toObject(), (QJsonObject{
            {"person_id", "person:1"}, {"followed", true}}));
        if (!timeout)
            fake.requests.first().first->disconnectFromHost();
        QTRY_VERIFY_WITH_TIMEOUT(reads == 1 && writes == 1, 11000);
        QCOMPARE(readCode, timeout ? ErrorCode::Timeout : ErrorCode::NotConnected);
        QCOMPARE(writeCode, ErrorCode::OutcomeUnknown);
        if (timeout) {
            PersonFakeServer::reply(fake.requests[0].first, fake.requests[0].second, updatesResponse({releaseUpdate()}));
            PersonFakeServer::reply(fake.requests[1].first, fake.requests[1].second, {});
            QTest::qWait(60);
            QCOMPARE(reads, 1);
            QCOMPARE(writes, 1);
        }
    }

    void followWriteIsUnknownAfterLogout() {
        PersonFakeServer fake; QVERIFY(fake.server.listen(QHostAddress::LocalHost));
        OnlineCareClient client; bool done = false; Status status;
        client.connectToServer("127.0.0.1", fake.server.serverPort(), [&](Status s) { status = s; done = true; });
        QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
        client.login("alice", "password", [&](auto r) { status = r.status; done = true; });
        QTRY_VERIFY(done); QVERIFY(status.ok());
        int callbacks = 0;
        client.setFollow("person:1", true, [&](Status s) { status = s; ++callbacks; });
        QTRY_COMPARE(fake.requests.size(), 1);
        client.logout([](Status) {});
        QCOMPARE(callbacks, 1); QCOMPARE(status.code, ErrorCode::OutcomeUnknown);
        PersonFakeServer::reply(fake.requests[0].first, fake.requests[0].second, {});
        QTest::qWait(100); QCOMPARE(callbacks, 1);
    }
    void malformedPersonResponseFails() {
        PersonFakeServer fake; QVERIFY(fake.server.listen(QHostAddress::LocalHost));
        OnlineCareClient client; bool done = false; Status status;
        client.connectToServer("127.0.0.1", fake.server.serverPort(), [&](Status s) { status = s; done = true; });
        QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
        client.login("alice", "password", [&](auto r) { status = r.status; done = true; });
        QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
        client.searchPeople({"person", {}}, [&](auto r) { status = r.status; done = true; });
        QTRY_COMPARE(fake.requests.size(), 1);
        const QJsonObject bad{{"person_id", "invalid id"}, {"name", "Person"}, {"roles", QJsonArray{"actor"}}, {"followed", false}};
        PersonFakeServer::reply(fake.requests[0].first, fake.requests[0].second, {{"page", QJsonObject{{"items", QJsonArray{bad}}}}});
        QTRY_VERIFY(done); QCOMPARE(status.code, ErrorCode::ProtocolError);
    }
    void rejectsMalformedNestedPeople() {
        PersonFakeServer fake; QVERIFY(fake.server.listen(QHostAddress::LocalHost));
        OnlineCareClient client; bool done = false; Status status;
        client.connectToServer("127.0.0.1", fake.server.serverPort(), [&](Status s) { status = s; done = true; });
        QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
        client.login("alice", "password", [&](auto r) { status = r.status; done = true; });
        QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
        client.getPersonWorks("person:1", {0, 10}, [&](auto r) { status = r.status; done = true; });
        QTRY_COMPARE(fake.requests.size(), 1);
        MediaRecord media; media.id = "m1"; media.fileName = "Film";
        const QJsonObject bad{{"person_id", "invalid id"}, {"name", "Person"}, {"roles", QJsonArray{"actor"}}, {"followed", false}};
        const QJsonObject resource{{"work_id", "work:1"}, {"source", QJsonObject{{"warehouse_id", "1"}, {"item_id", "2"}}}};
        const QJsonObject work{{"resource", resource}, {"media", media_json::toJson(media)}, {"people", QJsonArray{bad}}};
        PersonFakeServer::reply(fake.requests[0].first, fake.requests[0].second, {{"page", QJsonObject{{"items", QJsonArray{work}}}}});
        QTRY_VERIFY(done); QCOMPARE(status.code, ErrorCode::ProtocolError);
    }
    void acceptsAdvancingCursorOnShortPage() {
        PersonFakeServer fake; QVERIFY(fake.server.listen(QHostAddress::LocalHost));
        OnlineCareClient client; bool done = false; Status status;
        client.connectToServer("127.0.0.1", fake.server.serverPort(), [&](Status s) { status = s; done = true; });
        QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
        client.login("alice", "password", [&](auto r) { status = r.status; done = true; });
        QTRY_VERIFY(done); QVERIFY(status.ok()); done = false;
        Result<Page<PersonSummary>> result;
        client.searchPeople({"person", {0, 10}}, [&](auto r) { result = r; done = true; });
        QTRY_COMPARE(fake.requests.size(), 1);
        const QJsonObject person{{"person_id", "person:1"}, {"name", "Person"}, {"roles", QJsonArray{"actor"}}, {"followed", false}};
        PersonFakeServer::reply(fake.requests[0].first, fake.requests[0].second, {{"page", QJsonObject{{"items", QJsonArray{person}}, {"next_offset", "7"}}}});
        QTRY_VERIFY(done); QVERIFY(result.status.ok() && result.value && result.value->nextOffset); QCOMPARE(*result.value->nextOffset, 7);
    }
};
QTEST_GUILESS_MAIN(PersonClientTests)
#include "person_client_tests.moc"
