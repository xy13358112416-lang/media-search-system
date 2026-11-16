#include "online_care_client.h"
#include <QJsonArray>
#include <QPointer>
#include <QScopeGuard>
#include <QTcpServer>
#include <QtTest>
#include <memory>

using namespace care_contract;
using warehouse_contract::ErrorCode;

class TrendingFakeServer : public QObject {
public:
    QTcpServer server;
    QList<QPair<QPointer<QTcpSocket>, QJsonObject>> trends;
    QList<QJsonObject> cancellations;

    TrendingFakeServer() {
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (auto* socket = server.nextPendingConnection()) {
                auto decoder = std::make_shared<warehouse_protocol::FrameDecoder>();
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, decoder] {
                    QList<QJsonObject> frames; QString error;
                    if (!decoder->feed(socket->readAll(), frames, error)) { socket->abort(); return; }
                    for (const auto& frame : frames) {
                        const auto type = frame.value("type").toString();
                        if (type == "login") {
                            reply(socket, frame, {{"token", "token-alice"},
                                {"user", QJsonObject{{"user_id", "1"}, {"username", "alice"}}}});
                        } else if (type == "logout") reply(socket, frame, {});
                        else if (type == "get_trending") trends.append({socket, frame});
                        else if (type == "cancel_recommendation") {
                            cancellations.append(frame);
                            reply(socket, frame, {});
                        }
                    }
                });
            }
        });
    }

    static void reply(QTcpSocket* socket, const QJsonObject& frame, const QJsonObject& payload) {
        QString error;
        socket->write(warehouse_protocol::encodeFrame({{"type", frame.value("type").toString() + "_resp"},
            {"req_id", frame.value("req_id")}, {"payload", payload}}, error));
    }

    void replyTrend(int index, bool model) {
        const auto pending = trends.at(index);
        const auto payload = pending.second.value("payload").toObject();
        reply(pending.first, pending.second, {{"result", QJsonObject{
            {"kind", payload.value("kind")}, {"range", payload.value("range")},
            {"generated_at_utc", "200"}, {"summary", model ? "Model summary" : "Rules summary"},
            {"summary_from_model", model}, {"contains_synthetic_data", false}, {"items", QJsonArray{}}}}});
    }
};

namespace {
bool signIn(TrendingFakeServer& fake, OnlineCareClient& client) {
    std::optional<Status> connected;
    client.connectToServer("127.0.0.1", fake.server.serverPort(), [&](auto value) { connected = value; });
    if (!QTest::qWaitFor([&] { return connected.has_value(); }) || !connected->ok()) {
        client.disconnectFromServer(); return false;
    }
    std::optional<Status> loggedIn;
    client.login("alice", "password", [&](auto value) { loggedIn = value.status; });
    if (!QTest::qWaitFor([&] { return loggedIn.has_value(); }) || !loggedIn->ok()) {
        client.disconnectFromServer(); return false;
    }
    return true;
}
}

class TrendingClientTests : public QObject {
    Q_OBJECT
private slots:
    void explicitModesShareExistingResponseShape() {
        TrendingFakeServer fake; QVERIFY(fake.server.listen(QHostAddress::LocalHost));
        OnlineCareClient client; QVERIFY(signIn(fake, client));
        Result<TrendingResult> rules, model; int rulesCalls = 0, modelCalls = 0;
        auto cleanup = qScopeGuard([&] { client.disconnectFromServer(); });
        client.getTrending({TrendingKind::Works, {100, 200}, 5}, [&](auto result) { rules = result; ++rulesCalls; });
        client.getTrending({TrendingKind::SearchTerms, {100, 200}, 7, TrendingSummaryMode::Model},
            [&](auto result) { model = result; ++modelCalls; });
        QTRY_COMPARE(fake.trends.size(), 2);
        QCOMPARE(fake.trends[0].second.value("payload").toObject(),
            (QJsonObject{{"kind", "works"}, {"range", QJsonObject{{"begin_utc", "100"}, {"end_utc", "200"}}},
                         {"limit", 5}, {"summary_mode", "rules"}}));
        QCOMPARE(fake.trends[1].second.value("payload").toObject(),
            (QJsonObject{{"kind", "search_terms"}, {"range", QJsonObject{{"begin_utc", "100"}, {"end_utc", "200"}}},
                         {"limit", 7}, {"summary_mode", "model"}}));
        fake.replyTrend(1, true); fake.replyTrend(0, false);
        QTRY_COMPARE(rulesCalls, 1); QTRY_COMPARE(modelCalls, 1);
        QVERIFY(rules.status.ok()); QVERIFY(model.status.ok());
        QVERIFY(!rules.value->summaryFromModel); QVERIFY(model.value->summaryFromModel);
        QCOMPARE(model.value->summary, QStringLiteral("Model summary"));
        QCOMPARE(model.value->kind, TrendingKind::SearchTerms);
        QVERIFY(model.value->items.isEmpty());
    }

    void invalidModeDoesNotReachNetwork() {
        TrendingFakeServer fake; QVERIFY(fake.server.listen(QHostAddress::LocalHost));
        OnlineCareClient client; QVERIFY(signIn(fake, client));
        int calls = 0; Status status;
        auto cleanup = qScopeGuard([&] { client.disconnectFromServer(); });
        client.getTrending({TrendingKind::Works, {100, 200}, 5, static_cast<TrendingSummaryMode>(999)},
            [&](auto result) { status = result.status; ++calls; });
        QTRY_COMPARE(calls, 1);
        QCOMPARE(status.code, ErrorCode::InvalidArgument);
        QTest::qWait(50); QVERIFY(fake.trends.isEmpty()); QVERIFY(fake.cancellations.isEmpty());
    }

    void modelTimeoutCancelsOnlyModelAndIgnoresLateResponse() {
        TrendingFakeServer fake; QVERIFY(fake.server.listen(QHostAddress::LocalHost));
        OnlineCareClient client(nullptr, 150); QVERIFY(signIn(fake, client));
        int modelCalls = 0, rulesCalls = 0; Status modelStatus, rulesStatus;
        auto cleanup = qScopeGuard([&] { client.disconnectFromServer(); });
        client.getTrending({TrendingKind::Works, {100, 200}, 5, TrendingSummaryMode::Model},
            [&](auto result) { ++modelCalls; modelStatus = result.status; });
        client.getTrending({TrendingKind::Works, {100, 200}, 5},
            [&](auto result) { ++rulesCalls; rulesStatus = result.status; });
        QTRY_COMPARE(fake.trends.size(), 2);
        QTRY_COMPARE_WITH_TIMEOUT(modelCalls, 1, 1000);
        QCOMPARE(modelStatus.code, ErrorCode::Timeout);
        QCOMPARE(rulesCalls, 0);
        QTRY_COMPARE(fake.cancellations.size(), 1);
        QCOMPARE(fake.cancellations[0].value("payload").toObject().value("target_req_id"),
                 fake.trends[0].second.value("req_id"));
        fake.replyTrend(0, true); fake.replyTrend(1, false);
        QTRY_COMPARE(rulesCalls, 1); QVERIFY(rulesStatus.ok());
        QTest::qWait(200); QCOMPARE(modelCalls, 1); QCOMPARE(fake.cancellations.size(), 1);
    }

    void logoutCancelsModelOnlyAndIgnoresLateResponses() {
        TrendingFakeServer fake; QVERIFY(fake.server.listen(QHostAddress::LocalHost));
        OnlineCareClient client; QVERIFY(signIn(fake, client));
        int modelCalls = 0, rulesCalls = 0; Status modelStatus, rulesStatus;
        auto cleanup = qScopeGuard([&] { client.disconnectFromServer(); });
        client.getTrending({TrendingKind::SearchTerms, {100, 200}, 5, TrendingSummaryMode::Model},
            [&](auto result) { ++modelCalls; modelStatus = result.status; });
        client.getTrending({TrendingKind::SearchTerms, {100, 200}, 5},
            [&](auto result) { ++rulesCalls; rulesStatus = result.status; });
        QTRY_COMPARE(fake.trends.size(), 2);
        bool loggedOut = false; client.logout([&](Status status) { loggedOut = status.ok(); });
        QTRY_VERIFY(loggedOut);
        QCOMPARE(modelCalls, 1); QCOMPARE(rulesCalls, 1);
        QCOMPARE(modelStatus.code, ErrorCode::Cancelled); QCOMPARE(rulesStatus.code, ErrorCode::Cancelled);
        QTRY_COMPARE(fake.cancellations.size(), 1);
        QCOMPARE(fake.cancellations[0].value("payload").toObject().value("target_req_id"),
                 fake.trends[0].second.value("req_id"));
        fake.replyTrend(0, true); fake.replyTrend(1, false);
        QTest::qWait(100); QCOMPARE(modelCalls, 1); QCOMPARE(rulesCalls, 1);
    }
};
QTEST_GUILESS_MAIN(TrendingClientTests)
#include "trending_client_tests.moc"
