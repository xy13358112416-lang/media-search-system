#include "llm_trend_summary.h"
#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonArray>
#include <optional>

using care_contract::TrendingResult;
using llm_trend_summary::Client;
namespace {
TrendingResult input(bool works = true) {
    TrendingResult result;
    result.kind = works ? care_contract::TrendingKind::Works : care_contract::TrendingKind::SearchTerms;
    result.range = {1700000000, 1700600000}; result.generatedAtUtc = 1700600010;
    result.summary = QStringLiteral("原始规则摘要"); result.containsSyntheticData = true;
    care_contract::TrendingItem row; row.distinctUsers = 9007199254740993LL; row.effectiveEvents = 9007199254740995LL;
    if (works) {
        care_contract::ResourceSummary work;
        work.resource = {"private-work-id", {654321, 876543}};
        work.media.id = "private-media-id"; work.media.fileName = QStringLiteral("音乐合集");
        work.media.creator = "private-director";
        work.people.append({"private-person-id", "private-person", {}, true});
        row.work = work;
    } else row.term = QStringLiteral("公开音乐");
    result.items = {row}; return result;
}
QByteArray envelope(const QString& content, const QString& finish = "stop") {
    return QJsonDocument(QJsonObject{{"choices", QJsonArray{QJsonObject{{"finish_reason", finish},
        {"message", QJsonObject{{"content", content}}}}}}}).toJson(QJsonDocument::Compact);
}
QString validText() { return QStringLiteral("{\"headline\":\"音乐受到关注\",\"summary\":\"当前公开榜单出现音乐内容。\",\"row_ids\":[\"row-1\"]}"); }
class Server : public QTcpServer {
public:
    QByteArray body = envelope(validText()), received, extraHeaders;
    int calls = 0, status = 200; bool silent = false;
    Server() {
        connect(this, &QTcpServer::newConnection, this, [this] {
            auto* socket = nextPendingConnection();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                const auto data = socket->property("request").toByteArray() + socket->readAll();
                socket->setProperty("request", data);
                const auto boundary = data.indexOf("\r\n\r\n");
                if (boundary < 0 || socket->property("answered").toBool()) return;
                int length = 0;
                for (const auto& line : data.left(boundary).split('\n'))
                    if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toInt();
                if (data.size() - boundary - 4 < length) return;
                received = data; ++calls; socket->setProperty("answered", true);
                if (silent) return;
                socket->write("HTTP/1.1 " + QByteArray::number(status) + " Test\r\n" + extraHeaders
                    + "Content-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size())
                    + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
    }
    llm_trend_summary::Config config() const {
        llm_trend_summary::Config c; c.apiKey = "fake-secret-key"; c.timeoutMs = 300;
        c.endpoint = QUrl("http://127.0.0.1:" + QString::number(serverPort()) + "/chat/completions"); return c;
    }
    QJsonObject payload() const {
        return QJsonDocument::fromJson(received.mid(received.indexOf("\r\n\r\n") + 4)).object();
    }
};
}
class TrendTests : public QObject {
    Q_OBJECT
private slots:
    void missingKeyKeepsRuleSummaryAsynchronously() {
        Client client({}); std::optional<TrendingResult> result; auto original = input(); original.summaryFromModel = true;
        client.summarize(original, [&](TrendingResult r) { result = r; });
        QVERIFY(!result); QTRY_VERIFY(result.has_value());
        QVERIFY(!result->summaryFromModel); QCOMPARE(result->summary, original.summary);
        QCOMPARE(result->items.first().distinctUsers, original.items.first().distinctUsers);
    }
    void modelHeadlineAndSummaryKeepFactsAndPrivateMetadata() {
        Server server; QVERIFY(server.listen(QHostAddress::LocalHost)); Client client(server.config());
        const auto original = input(); std::optional<TrendingResult> result; int calls = 0;
        client.summarize(original, [&](TrendingResult r) { result = r; ++calls; });
        QTRY_VERIFY(result.has_value()); QVERIFY(result->summaryFromModel);
        QVERIFY(result->summary.contains(QStringLiteral("音乐受到关注")));
        QVERIFY(result->summary.contains(QStringLiteral("当前公开榜单出现音乐内容。")));
        QVERIFY(result->summary.contains("9007199254740993")); QVERIFY(result->summary.contains("9007199254740995"));
        QCOMPARE(result->items.size(), 1); QCOMPARE(result->items.first().work->resource.workId, QStringLiteral("private-work-id"));
        QCOMPARE(result->items.first().work->resource.source.warehouseId, qint64(654321));
        QCOMPARE(result->items.first().effectiveEvents, original.items.first().effectiveEvents);
        QCOMPARE(result->range.beginUtc, original.range.beginUtc); QCOMPARE(result->range.endUtc, original.range.endUtc);
        QCOMPARE(result->generatedAtUtc, original.generatedAtUtc); QCOMPARE(result->containsSyntheticData, original.containsSyntheticData);
        const auto body = server.payload(); const auto messages = body["messages"].toArray();
        const auto content = messages.last().toObject()["content"].toString();
        QVERIFY(!content.contains("private-")); QVERIFY(!content.contains("654321")); QVERIFY(!content.contains("fake-secret-key"));
        const auto rows = QJsonDocument::fromJson(content.toUtf8()).object()["rows"].toArray();
        QCOMPARE(rows.first().toObject()["id"].toString(), QStringLiteral("row-1"));
        QCOMPARE(rows.first().toObject()["distinct_users"].toString(), QStringLiteral("9007199254740993"));
        QVERIFY(messages.first().toObject()["content"].toString().contains("不可信"));
        QTest::qWait(20); QCOMPARE(calls, 1); QCOMPARE(server.calls, 1);
    }
    void emptyInputNeverCallsApi() {
        Server server; QVERIFY(server.listen(QHostAddress::LocalHost)); Client client(server.config());
        auto original = input(); original.items.clear(); std::optional<TrendingResult> result;
        client.summarize(original, [&](TrendingResult r) { result = r; });
        QTRY_VERIFY(result.has_value()); QVERIFY(!result->summaryFromModel); QCOMPARE(result->summary, original.summary); QCOMPARE(server.calls, 0);
    }
    void searchTermsAreAggregateOnly() {
        Server server; QVERIFY(server.listen(QHostAddress::LocalHost)); Client client(server.config());
        std::optional<TrendingResult> result; client.summarize(input(false), [&](TrendingResult r) { result = r; });
        QTRY_VERIFY(result.has_value()); QVERIFY(result->summaryFromModel);
        const auto data = QJsonDocument::fromJson(server.payload()["messages"].toArray().last().toObject()["content"].toString().toUtf8()).object();
        QCOMPARE(data["kind"].toString(), QStringLiteral("search_terms"));
        const auto row = data["rows"].toArray().first().toObject();
        QCOMPARE(row["label"].toString(), QStringLiteral("公开音乐")); QCOMPARE(row.size(), 4);
    }
    void rejectedResponses_data() {
        QTest::addColumn<int>("status"); QTest::addColumn<QByteArray>("body");
        QTest::newRow("http-401") << 401 << QByteArray("fake-secret-key provider error");
        QTest::newRow("http-429") << 429 << QByteArray("{}");
        QTest::newRow("http-500") << 500 << QByteArray("{}");
        QTest::newRow("malformed-envelope") << 200 << QByteArray("no json");
        QTest::newRow("empty") << 200 << envelope("{}");
        QTest::newRow("missing-content") << 200 << QByteArray("{\"choices\":[{}]}");
        QTest::newRow("truncated") << 200 << envelope(validText(), "length");
        QTest::newRow("unknown-row") << 200 << envelope(QString(validText()).replace("row-1", "private-id"));
        QTest::newRow("duplicate-row") << 200 << envelope(QString(validText()).replace("[\"row-1\"]", "[\"row-1\",\"row-1\"]"));
        QTest::newRow("empty-evidence") << 200 << envelope(QString(validText()).replace("[\"row-1\"]", "[]"));
        QTest::newRow("key-echo") << 200 << envelope(QString(validText()).replace(QStringLiteral("音乐受到关注"), "fake-secret-key"));
        QTest::newRow("invented-count") << 200 << envelope(QString(validText()).replace(QStringLiteral("音乐受到关注"), "999 users"));
        QTest::newRow("unicode-number") << 200 << envelope(QString(validText()).replace(QStringLiteral("音乐受到关注"), QStringLiteral("９９人")));
        QTest::newRow("markup") << 200 << envelope(QString(validText()).replace(QStringLiteral("音乐受到关注"), "<b>news</b>"));
        QTest::newRow("control") << 200 << envelope(QString(validText()).replace(QStringLiteral("音乐受到关注"), "x\\u0000y"));
        QTest::newRow("long-text") << 200 << envelope(QString(validText()).replace(QStringLiteral("音乐受到关注"), QString(100, 'x')));
        QTest::newRow("oversized-reply") << 200 << QByteArray(70000, 'x');
    }
    void rejectedResponses() {
        QFETCH(int, status); QFETCH(QByteArray, body); Server server; QVERIFY(server.listen(QHostAddress::LocalHost));
        server.status = status; server.body = body; Client client(server.config()); std::optional<TrendingResult> result; int calls = 0;
        client.summarize(input(), [&](TrendingResult r) { result = r; ++calls; }); QTRY_VERIFY(result.has_value());
        QVERIFY(!result->summaryFromModel); QCOMPARE(result->summary, input().summary);
        QVERIFY(!result->summary.contains("fake-secret-key")); QTest::qWait(20); QCOMPARE(calls, 1); QCOMPARE(server.calls, 1);
    }
    void noRedirectsOrPlainRemoteHttp() {
        Server destination; QVERIFY(destination.listen(QHostAddress::LocalHost));
        Server redirect; QVERIFY(redirect.listen(QHostAddress::LocalHost)); redirect.status = 302;
        redirect.extraHeaders = "Location: " + destination.config().endpoint.toString().toUtf8() + "\r\n";
        Client client(redirect.config()); std::optional<TrendingResult> result;
        client.summarize(input(), [&](TrendingResult r) { result = r; }); QTRY_VERIFY(result.has_value());
        QVERIFY(!result->summaryFromModel); QCOMPARE(destination.calls, 0);
        auto config = redirect.config(); config.endpoint = QUrl("http://example.invalid/endpoint"); Client remote(config);
        result.reset(); remote.summarize(input(), [&](TrendingResult r) { result = r; }); QTRY_VERIFY(result.has_value()); QVERIFY(!result->summaryFromModel);
    }
    void boundedAggregateRequest() {
        Server server; QVERIFY(server.listen(QHostAddress::LocalHost)); Client client(server.config());
        auto original = input(); original.items.clear();
        for (int i = 0; i < 80; ++i) { auto row = input().items.first(); row.work->media.fileName = QString(5000, QChar(0x4e2d)); original.items.append(row); }
        std::optional<TrendingResult> result; client.summarize(original, [&](TrendingResult r) { result = r; }); QTRY_VERIFY(result.has_value());
        QCOMPARE(result->items.size(), 80); QCOMPARE(server.calls, 1);
        const auto body = server.payload(); QVERIFY(QJsonDocument(body).toJson(QJsonDocument::Compact).size() <= 32768);
        const auto rows = QJsonDocument::fromJson(body["messages"].toArray().last().toObject()["content"].toString().toUtf8()).object()["rows"].toArray();
        QCOMPARE(rows.size(), 50); QVERIFY(rows.first().toObject()["label"].toString().size() <= 160);
        QCOMPARE(rows.last().toObject()["id"].toString(), QStringLiteral("row-50"));
    }
    void aggregateCannotAccidentallySendConfiguredSecret() {
        Server server; QVERIFY(server.listen(QHostAddress::LocalHost)); Client client(server.config());
        auto original = input(); original.items.first().work->media.fileName = "fake-secret-key";
        std::optional<TrendingResult> result;
        client.summarize(original, [&](TrendingResult r) { result = r; }); QTRY_VERIFY(result.has_value());
        QVERIFY(!result->summaryFromModel); QCOMPARE(result->summary, original.summary); QCOMPARE(server.calls, 0);
    }
    void requestByteBudgetIncludesJsonEscaping() {
        Server server; QVERIFY(server.listen(QHostAddress::LocalHost)); Client client(server.config());
        auto original = input(); original.items.clear();
        for (int i = 0; i < 50; ++i) { auto row = input().items.first(); row.work->media.fileName = QString(160, QChar('"')); original.items.append(row); }
        std::optional<TrendingResult> result;
        client.summarize(original, [&](TrendingResult r) { result = r; }); QTRY_VERIFY(result.has_value());
        QCOMPARE(server.calls, 0); QVERIFY(!result->summaryFromModel); QCOMPARE(result->summary, original.summary); QCOMPARE(result->items.size(), 50);
    }
    void invalidAggregateDoesNotCrossBoundary_data() {
        QTest::addColumn<int>("failure");
        QTest::newRow("privacy-threshold") << 0; QTest::newRow("negative-events") << 1;
        QTest::newRow("missing-work") << 2; QTest::newRow("mixed-kind") << 3;
        QTest::newRow("blank-label") << 4;
    }
    void invalidAggregateDoesNotCrossBoundary() {
        QFETCH(int, failure); Server server; QVERIFY(server.listen(QHostAddress::LocalHost)); Client client(server.config());
        auto original = input(); auto& row = original.items.first();
        if (failure == 0) row.distinctUsers = 2;
        if (failure == 1) row.effectiveEvents = -1;
        if (failure == 2) row.work.reset();
        if (failure == 3) row.term = "mixed";
        if (failure == 4) row.work->media.fileName = " ";
        std::optional<TrendingResult> result;
        client.summarize(original, [&](TrendingResult r) { result = r; }); QTRY_VERIFY(result.has_value());
        QCOMPARE(server.calls, 0); QVERIFY(!result->summaryFromModel); QCOMPARE(result->summary, original.summary);
    }
    void timeoutCancellationAndDestruction() {
        Server server; QVERIFY(server.listen(QHostAddress::LocalHost)); server.silent = true;
        auto config = server.config(); config.timeoutMs = 30; Client client(config); std::optional<TrendingResult> result;
        client.summarize(input(), [&](TrendingResult r) { result = r; }); QTRY_VERIFY(result.has_value()); QVERIFY(!result->summaryFromModel);
        bool called = false; auto queued = client.summarize(input(), [&](TrendingResult) { called = true; }); client.cancel(queued);
        Client noKey({}); auto noKeyId = noKey.summarize(input(), [&](TrendingResult) { called = true; }); noKey.cancel(noKeyId);
        Client active(server.config()); const int before = server.calls;
        auto liveId = active.summarize(input(), [&](TrendingResult) { called = true; }); QTRY_COMPARE(server.calls, before + 1); active.cancel(liveId);
        { Client destroyed(server.config()); destroyed.summarize(input(), [&](TrendingResult) { called = true; }); QTRY_COMPARE(server.calls, before + 2); }
        QTest::qWait(350); QVERIFY(!called);
    }
};
QTEST_GUILESS_MAIN(TrendTests)
#include "llm_trend_summary_tests.moc"
