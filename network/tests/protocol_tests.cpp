#include "media_json.h"
#include "warehouse_protocol.h"
#include "warehouse_contract.h"
#include <QJsonArray>
#include <QtTest>
#include <limits>

class ProtocolTests : public QObject {
    Q_OBJECT
private slots:
    void mediaRoundTrip() {
        MediaRecord input;
        input.id = "M000001"; input.fileName = QStringLiteral("作品,\"甲\".mp4"); input.fileType = "MP4";
        input.fileSize = std::numeric_limits<quint64>::max(); input.director = QStringLiteral("导演");
        input.actors = {QStringLiteral("演员甲"), QStringLiteral("演员乙")}; input.viewCount = 9007199254740993ULL;
        input.releaseDate = QDate(2024, 2, 29); input.awards = {QStringLiteral("奖项")}; input.category = QStringLiteral("纪录片");
        const QJsonObject json = media_json::toJson(input);
        QCOMPARE(json.value("file_size").toString(), QStringLiteral("18446744073709551615"));
        QCOMPARE(json.value("view_count").toString(), QStringLiteral("9007199254740993"));
        MediaRecord output; QString error;
        QVERIFY2(media_json::fromJson(json, output, error), qPrintable(error));
        QCOMPARE(output.id, input.id); QCOMPARE(output.fileName, input.fileName); QCOMPARE(output.fileType, input.fileType);
        QCOMPARE(output.fileSize, input.fileSize); QCOMPARE(output.director, input.director); QCOMPARE(output.actors, input.actors);
        QCOMPARE(output.viewCount, input.viewCount); QCOMPARE(output.releaseDate, input.releaseDate);
        QCOMPARE(output.awards, input.awards); QCOMPARE(output.category, input.category);
    }
    void invalidMediaDoesNotOverwriteOutput() {
        MediaRecord record; record.id = "old"; QString error;
        QVERIFY(!media_json::fromJson(QJsonObject{{"id", "new"}}, record, error));
        QCOMPARE(record.id, QStringLiteral("old")); QVERIFY(!error.isEmpty());
        MediaRecord valid; valid.id = "x"; valid.fileName = "x.mp4"; valid.fileType = "MP4"; valid.releaseDate = QDate(2020,1,1);
        auto json = media_json::toJson(valid); json.insert("view_count", "18446744073709551616");
        QVERIFY(!media_json::fromJson(json, record, error)); QCOMPARE(record.id, QStringLiteral("old"));
        json = media_json::toJson(valid); json.insert("view_count", "1\n");
        QVERIFY(!media_json::fromJson(json, record, error));
        json = media_json::toJson(valid); json.insert("actors", QJsonArray{42});
        QVERIFY(!media_json::fromJson(json, record, error));
    }
    void fragmentedAndCoalescedFrames() {
        const QJsonObject first{{"type", "login"}, {"req_id", "1"}};
        const QJsonObject second{{"type", "logout"}, {"req_id", "2"}};
        QString error; const QByteArray a = warehouse_protocol::encodeFrame(first, error);
        const QByteArray b = warehouse_protocol::encodeFrame(second, error);
        QVERIFY(!a.isEmpty() && !b.isEmpty());
        QCOMPARE(static_cast<unsigned char>(a.at(0)), 0); QCOMPARE(static_cast<unsigned char>(a.at(1)), 0);
        warehouse_protocol::FrameDecoder decoder; QList<QJsonObject> result;
        QVERIFY(decoder.feed(a.left(2), result, error)); QVERIFY(result.isEmpty());
        QVERIFY(decoder.feed(a.mid(2, 4), result, error)); QVERIFY(result.isEmpty());
        QVERIFY(decoder.feed(a.mid(6) + b, result, error)); QCOMPARE(result.size(), 2);
        QCOMPARE(result.at(0), first); QCOMPARE(result.at(1), second);
    }
    void rejectsInvalidAndOversizedFrames() {
        warehouse_protocol::FrameDecoder decoder; QList<QJsonObject> result; QString error;
        QByteArray header(4, '\0'); header[1] = char(0x40); header[3] = char(1);
        QVERIFY(!decoder.feed(header, result, error)); QVERIFY(!error.isEmpty());
        decoder.reset(); error.clear();
        QVERIFY(!decoder.feed(QByteArray::fromHex("000000025b5d"), result, error));
        QVERIFY(!error.isEmpty());
        error.clear(); QVERIFY(!decoder.feed({}, result, error)); QVERIFY(!error.isEmpty());
        decoder.reset(); error.clear();
        QVERIFY(!decoder.feed(QByteArray::fromHex("00000000"), result, error));
        QJsonObject tooLarge{{"data", QString(4 * 1024 * 1024, QLatin1Char('x'))}};
        QVERIFY(warehouse_protocol::encodeFrame(tooLarge, error).isEmpty()); QVERIFY(!error.isEmpty());
    }
};
QTEST_GUILESS_MAIN(ProtocolTests)
#include "protocol_tests.moc"
