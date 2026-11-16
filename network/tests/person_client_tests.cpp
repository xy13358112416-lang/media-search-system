#include "online_care_client.h"
#include "media_json.h"
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
                        if (type == "login") reply(socket, frame, {{"token", "token"}, {"user", QJsonObject{{"user_id", "1"}, {"username", "alice"}}}});
                        else if (type == "logout") reply(socket, frame, {});
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
};

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
        PersonFakeServer::reply(fake.requests[2].first, fake.requests[2].second, {{"page", QJsonObject{{"items", QJsonArray{work}}}}});
        QTRY_VERIFY(done); QVERIFY(works.status.ok() && works.value); QCOMPARE(works.value->items.first().resource.workId, QStringLiteral("work:1"));
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
