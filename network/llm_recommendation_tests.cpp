#include "llm_recommendation.h"
#include "llm_warehouse_adapter.h"
#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QFile>
#include <optional>

using namespace llm_recommendation;
namespace {
Request request() {
    Request r; r.context = QStringLiteral("音乐收藏");
    MediaRecord a; a.fileName = "song.mp3"; a.category = QStringLiteral("音乐"); a.viewCount = 3;
    MediaRecord b; b.fileName = "film.mp4"; b.viewCount = 9;
    r.candidates = {{"warehouse1:item1", a}, {"warehouse2:item1", b}};
    return r;
}
QByteArray completion(const QString& content, const QString& finish = "stop") {
    return QJsonDocument(QJsonObject{{"choices", QJsonArray{QJsonObject{
        {"finish_reason", finish}, {"message", QJsonObject{{"content", content}}}}}}}).toJson();
}
class Server : public QTcpServer {
public:
    QByteArray body, received;
    int status = 200, calls = 0;
    bool silent = false;
    Server() {
        QObject::connect(this, &QTcpServer::newConnection, this, [this] {
            auto* socket = nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            QObject::connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                auto data = socket->property("request").toByteArray() + socket->readAll();
                socket->setProperty("request", data);
                const int boundary = data.indexOf("\r\n\r\n");
                if (boundary < 0 || socket->property("answered").toBool()) return;
                int length = 0;
                for (const auto& line : data.left(boundary).split('\n'))
                    if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toInt();
                if (data.size() - boundary - 4 < length) return;
                socket->setProperty("answered", true); received = data; ++calls;
                if (silent) return;
                socket->write("HTTP/1.1 " + QByteArray::number(status) + " Test\r\nContent-Type: application/json\r\nContent-Length: "
                              + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
    }
    Config config() {
        Config c; c.apiKey = "fake-test-key";
        c.endpoint = QUrl("http://127.0.0.1:" + QString::number(serverPort()) + "/chat/completions");
        c.timeoutMs = 500; return c;
    }
};
}
class LlmTests : public QObject {
    Q_OBJECT
private slots:
    void warehouseKeysDoNotConfuseLocalMediaIds() {
        warehouse_contract::WarehouseDetail first, second;
        first.warehouse = {10, QStringLiteral("音乐收藏"), 1, warehouse_contract::Visibility::Private};
        second.warehouse = {20, QStringLiteral("公开收藏"), 2, warehouse_contract::Visibility::Public};
        auto media = request().candidates.first().media;
        media.id = "same-local-id";
        first.items = {warehouse_contract::WarehouseItem{7, media}};
        second.items = {warehouse_contract::WarehouseItem{7, media}};
        const auto input = fromWarehouses(first.warehouse, {first, second}, 2);
        QCOMPARE(input.context, first.warehouse.name);
        QCOMPARE(input.candidates.size(), 2);
        QCOMPARE(input.candidates[0].key, QStringLiteral("10:7"));
        QCOMPARE(input.candidates[1].key, QStringLiteral("20:7"));
        Result model{false, {{"20:7", "公开条目"}, {"10:7", "本人条目"}, {"20:7", "重复"}, {"99:7", "越界"}}, "message"};
        auto output = toWarehouses(model, {first, second});
        QCOMPARE(output.items.size(), 2);
        QCOMPARE(output.items[0].warehouseId, qint64(20));
        QCOMPARE(output.items[1].warehouseId, qint64(10));
        QCOMPARE(output.items[0].item.media.id, QStringLiteral("same-local-id"));
        // A resource may be edited while the model request is in flight.
        second.items[0].media.id = QStringLiteral("updated-local-id");
        second.items[0].media.fileName = QStringLiteral("updated-title.mp3");
        output = toWarehouses(model, {first, second});
        QCOMPARE(output.items[0].item.media.id, QStringLiteral("updated-local-id"));
        QCOMPARE(output.items[0].item.media.fileName, QStringLiteral("updated-title.mp3"));
        // Server reauthorization no longer includes first warehouse: old result is dropped.
        output = toWarehouses(model, {second});
        QCOMPARE(output.items.size(), 1);
        second.items.clear();
        QVERIFY(toWarehouses(model, {second}).items.isEmpty());
    }
    void warehouseAdapterPreservesFallbackAndRejectsInvalidIds() {
        warehouse_contract::WarehouseDetail invalid, good;
        invalid.warehouse.warehouseId = 0;
        good.warehouse.warehouseId = 8;
        auto media = request().candidates.first().media;
        invalid.items = {warehouse_contract::WarehouseItem{1, media}};
        good.items = {warehouse_contract::WarehouseItem{0, media}, warehouse_contract::WarehouseItem{2, media}};
        QCOMPARE(fromWarehouses(good.warehouse, {invalid, good, good}).candidates.size(), 1);
        auto output = toWarehouses({true, {{"8:2", "规则"}}, "降级"}, {good});
        QVERIFY(output.fallback); QCOMPARE(output.message, QStringLiteral("降级"));
        QCOMPARE(output.items.first().item.itemId, qint64(2));
    }
    void missingKeyIsAsyncAndUsesCandidateHeat() {
        Client client(Config{}); bool called = false; Result result;
        client.recommend(request(), [&](Result r) { result = r; called = true; });
        QVERIFY(!called); QTRY_VERIFY(called);
        QVERIFY(result.fallback); QCOMPARE(result.items.size(), 2);
        QCOMPARE(result.items.first().key, QStringLiteral("warehouse1:item1"));
    }
    void coldRelevantCandidateSurvivesPoolLimit() {
        Client client(Config{}); auto r = request();
        for (int i = 0; i < 40; ++i) {
            auto hot = r.candidates.last(); hot.key = QStringLiteral("hot:%1").arg(i);
            hot.media.viewCount = 100; r.candidates.append(hot);
        }
        r.candidates.first().media.viewCount = 0;
        std::optional<Result> result;
        client.recommend(r, [&](Result value) { result = value; });
        QTRY_VERIFY(result.has_value()); QVERIFY(result->fallback);
        QCOMPARE(result->items.first().key, QStringLiteral("warehouse1:item1"));
        QCOMPARE(result->items.size(), 6);
    }
    void validatesIdsAndSerializesOnlyContentMetadata() {
        Server server; QVERIFY(server.listen(QHostAddress::LocalHost));
        server.body = completion(QStringLiteral("```json\n{\"recommendations\":[{\"id\":\"warehouse1:item1\",\"reason\":\"音乐类别相关\"},{\"id\":\"unknown\",\"reason\":\"虚构\"},{\"id\":\"warehouse1:item1\",\"reason\":\"重复\"}]}\n```"));
        Client client(server.config()); std::optional<Result> result; int completions = 0;
        client.recommend(request(), [&](Result r) { result = r; ++completions; });
        QTRY_VERIFY(result.has_value()); QVERIFY(!result->fallback);
        QCOMPARE(result->items.size(), 1); QCOMPARE(result->items.first().key, QStringLiteral("warehouse1:item1"));
        const int boundary = server.received.indexOf("\r\n\r\n");
        auto body = QJsonDocument::fromJson(server.received.mid(boundary + 4)).object();
        QVERIFY(body["response_format"].toObject()["type"] == "json_object");
        const QString input = body["messages"].toArray().last().toObject()["content"].toString();
        const auto sent = QJsonDocument::fromJson(input.toUtf8()).object()["candidates"].toArray();
        QCOMPARE(sent.size(), 2);
        QVERIFY(!sent.first().toObject().contains("viewCount"));
        QVERIFY(!sent.first().toObject().contains("fileSize"));
        QVERIFY(!input.contains("fake-test-key"));
        QTest::qWait(20); QCOMPARE(completions, 1);
    }
    void failures_data() {
        QTest::addColumn<int>("status"); QTest::addColumn<QByteArray>("body");
        QTest::newRow("unauthorized") << 401 << QByteArray("secret details");
        QTest::newRow("rate-limited") << 429 << QByteArray("{}");
        QTest::newRow("server-error") << 500 << QByteArray("{}");
        QTest::newRow("bad-json") << 200 << QByteArray("not json");
        QTest::newRow("unknown-ids") << 200 << completion("{\"recommendations\":[{\"id\":\"other-private-resource\",\"reason\":\"bad\"}]}");
        QTest::newRow("missing-reason") << 200 << completion("{\"recommendations\":[{\"id\":\"warehouse1:item1\"}]}");
        QTest::newRow("missing-list") << 200 << completion("{}");
        QTest::newRow("truncated") << 200 << completion("{}", "length");
        QTest::newRow("oversized") << 200 << QByteArray(70000, 'x');
    }
    void failures() {
        QFETCH(int, status); QFETCH(QByteArray, body);
        Server server; QVERIFY(server.listen(QHostAddress::LocalHost)); server.status = status; server.body = body;
        Client client(server.config()); std::optional<Result> result; int calls = 0;
        client.recommend(request(), [&](Result r) { result = r; ++calls; });
        QTRY_VERIFY(result.has_value()); QVERIFY(result->fallback); QCOMPARE(result->items.size(), 2);
        QVERIFY(!result->message.contains("secret details")); QVERIFY(!result->message.contains("fake-test-key"));
        QCOMPARE(calls, 1); QCOMPARE(server.calls, 1);
    }
    void validEmptyMeansNoMatchRatherThanHotFallback() {
        Server server; QVERIFY(server.listen(QHostAddress::LocalHost));
        server.body = completion("{\"recommendations\":[]}");
        Client client(server.config()); std::optional<Result> result;
        client.recommend(request(), [&](Result r) { result = r; });
        QTRY_VERIFY(result.has_value());
        QVERIFY(!result->fallback);
        QVERIFY(result->items.isEmpty());
        QVERIFY(result->message.contains(QStringLiteral("没有找到")));
        QCOMPARE(server.calls, 1);
    }
    void timeoutAndCancellation() {
        Server server; QVERIFY(server.listen(QHostAddress::LocalHost)); server.silent = true;
        auto config = server.config(); config.timeoutMs = 30;
        Client client(config); std::optional<Result> result;
        client.recommend(request(), [&](Result r) { result = r; });
        QTRY_VERIFY(result.has_value()); QVERIFY(result->fallback);
        bool called = false;
        const auto id = client.recommend(request(), [&](Result) { called = true; });
        client.cancel(id); QTest::qWait(70); QVERIFY(!called);
        Client live(server.config());
        const int before = server.calls;
        const auto activeId = live.recommend(request(), [&](Result) { called = true; });
        QTRY_COMPARE(server.calls, before + 1);
        live.cancel(activeId);
        {
            Client destroyed(server.config());
            destroyed.recommend(request(), [&](Result) { called = true; });
            QTRY_COMPARE(server.calls, before + 2);
        }
        QTest::qWait(550); QVERIFY(!called); // Even after the original deadline, no stale completion.
        Client noKey(Config{});
        const auto queued = noKey.recommend(request(), [&](Result) { called = true; });
        noKey.cancel(queued); QTest::qWait(10); QVERIFY(!called);
    }
    void invalidCandidatesAndZeroLimit() {
        Client client(Config{}); auto r = request(); r.candidates.append(r.candidates.first());
        r.candidates.append(Candidate{"empty", {}}); std::optional<Result> result;
        client.recommend(r, [&](Result value) { result = value; });
        QTRY_VERIFY(result.has_value()); QCOMPARE(result->items.size(), 2);
        result.reset(); r.limit = 0;
        client.recommend(r, [&](Result value) { result = value; });
        QTRY_VERIFY(result.has_value()); QVERIFY(result->items.isEmpty());
    }
    void configEnvironmentOverridesFile() {
        QTemporaryDir dir; QFile file(dir.filePath("llm_config.json")); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{\"api_key\":\"file-key\",\"model\":\"configured-model\"}"); file.close();
        const bool hadKey = qEnvironmentVariableIsSet("MEDIA_SERVER_DEEPSEEK_API_KEY"); const auto oldKey = qgetenv("MEDIA_SERVER_DEEPSEEK_API_KEY");
        qputenv("MEDIA_SERVER_DEEPSEEK_API_KEY", "environment-key");
        const auto config = loadConfig(file.fileName());
        if (hadKey) qputenv("MEDIA_SERVER_DEEPSEEK_API_KEY", oldKey); else qunsetenv("MEDIA_SERVER_DEEPSEEK_API_KEY");
        QCOMPARE(config.apiKey, QStringLiteral("environment-key"));
        QCOMPARE(config.model, QStringLiteral("configured-model"));
    }
    void explicitConfigPathOverridesServerEnvironmentPath() {
        QTemporaryDir dir;
        QFile file(dir.filePath("server-config.json")); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{\"model\":\"server-model\"}"); file.close();
        const bool existed = qEnvironmentVariableIsSet("MEDIA_SERVER_DEEPSEEK_CONFIG_PATH");
        const auto old = qgetenv("MEDIA_SERVER_DEEPSEEK_CONFIG_PATH");
        qputenv("MEDIA_SERVER_DEEPSEEK_CONFIG_PATH", file.fileName().toUtf8());
        const auto inferred = loadConfig();
        const auto explicitPath = loadConfig(dir.filePath("missing.json"));
        if (existed) qputenv("MEDIA_SERVER_DEEPSEEK_CONFIG_PATH", old); else qunsetenv("MEDIA_SERVER_DEEPSEEK_CONFIG_PATH");
        QCOMPARE(inferred.model, QStringLiteral("server-model"));
        QCOMPARE(explicitPath.model, Config{}.model);
    }
    void ignoresOtherProjectsKey() {
        QTemporaryDir dir;
        const bool hadServerKey = qEnvironmentVariableIsSet("MEDIA_SERVER_DEEPSEEK_API_KEY");
        const auto oldServerKey = qgetenv("MEDIA_SERVER_DEEPSEEK_API_KEY");
        const bool hadGeneralKey = qEnvironmentVariableIsSet("DEEPSEEK_API_KEY");
        const auto oldGeneralKey = qgetenv("DEEPSEEK_API_KEY");
        qunsetenv("MEDIA_SERVER_DEEPSEEK_API_KEY");
        qputenv("DEEPSEEK_API_KEY", "other-project-test-key");
        const auto isolated = loadConfig(dir.filePath("missing.json"));
        qputenv("MEDIA_SERVER_DEEPSEEK_API_KEY", "server-test-key");
        const auto configured = loadConfig(dir.filePath("missing.json"));
        if (hadServerKey) qputenv("MEDIA_SERVER_DEEPSEEK_API_KEY", oldServerKey); else qunsetenv("MEDIA_SERVER_DEEPSEEK_API_KEY");
        if (hadGeneralKey) qputenv("DEEPSEEK_API_KEY", oldGeneralKey); else qunsetenv("DEEPSEEK_API_KEY");
        QVERIFY(isolated.apiKey.isEmpty());
        QCOMPARE(configured.apiKey, QStringLiteral("server-test-key"));
    }
};
QTEST_GUILESS_MAIN(LlmTests)
#include "llm_recommendation_tests.moc"
