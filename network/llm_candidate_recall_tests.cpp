#include "llm_candidate_recall.h"
#include "llm_warehouse_adapter.h"
#include <QtTest>
#ifdef RECALL_CSV
#include "medialibrary.h"
#include <QTemporaryDir>
#include <QFile>
#endif
#include <algorithm>

using namespace llm_recommendation;
namespace {
Candidate candidate(QString key, QString title, QString category, quint64 heat = 0) {
    MediaRecord record; record.fileName = title; record.category = category; record.viewCount = heat;
    return {key, record};
}
QStringList keys(const QList<Candidate>& selected) {
    QStringList result; for (const auto& item : selected) result.append(item.key); return result;
}
}
class RecallTests : public QObject {
    Q_OBJECT
private slots:
#ifdef RECALL_CSV
    void realCatalogKeepsExactTitleDespiteZeroHeatAndOpaqueKeys() {
        QTemporaryDir dir; const auto path = dir.filePath("records.csv");
        QVERIFY(QFile::copy(QString::fromUtf8(RECALL_CSV), path));
        MediaLibrary library(path, {}); QVERIFY(library.load().success);
        const auto records = library.records(); QCOMPARE(records.size(), 500);
        warehouse_contract::WarehouseDetail source;
        source.warehouse = {1, "source", 1, warehouse_contract::Visibility::Public};
        qint64 index = 0; QString expected;
        for (const auto& record : records) {
            source.items.append({++index, record});
            if (record.fileName == "Pygmalion") expected = "1:" + QString::number(index);
        }
        QVERIFY(!expected.isEmpty());
        warehouse_contract::WarehouseRecommendationRequest query{"Pygmalion", std::nullopt, 30};
        const auto pool = selectCandidates(fromWarehouses(query, {source}));
        QCOMPARE(pool.size(), 30); QCOMPARE(pool.first().key, expected);
    }
#endif
    void textRecall_data() {
        QTest::addColumn<QString>("query"); QTest::addColumn<QString>("title");
        QTest::addColumn<QString>("director"); QTest::addColumn<QStringList>("actors");
        QTest::newRow("unicode-title") << QStringLiteral("  ＰＹＧＭＡＬＩＯＮ  ") << QString("Pygmalion") << QString() << QStringList();
        QTest::newRow("english-extension") << QString("space voyage") << QString("Space Voyage.mkv") << QString() << QStringList();
        QTest::newRow("chinese-title") << QStringLiteral("推荐雨夜钢琴") << QStringLiteral("雨夜钢琴.mp3") << QString() << QStringList();
        QTest::newRow("director") << QStringLiteral("我想看李安导演的作品") << QString("Unknown title") << QStringLiteral("李安") << QStringList();
        QTest::newRow("actor") << QString("Films with ROBERT DE NIRO") << QString("Unknown title") << QString() << QStringList{"Robert De Niro"};
    }
    void textRecall() {
        QFETCH(QString, query); QFETCH(QString, title); QFETCH(QString, director); QFETCH(QStringList, actors);
        Request request; request.context = query;
        request.candidates = {candidate("a:hot", "Other", "Other", 100)};
        auto target = candidate("z:target", title, "Target"); target.media.director = director; target.media.actors = actors;
        request.candidates.append(target);
        QCOMPARE(selectCandidates(request, 1).first().key, QStringLiteral("z:target"));
    }
    void englishTitlesDoNotMatchInsideUnrelatedWords() {
        Request request; request.context = "with music";
        request.candidates = {candidate("a:short", "It", "Movie"), candidate("b:hot", "Piano", "Music", 100)};
        QCOMPARE(selectCandidates(request, 1).first().key, QStringLiteral("b:hot"));
    }
    void actorMetadataIsBoundedAndQueryAware() {
        const QStringList actors{"First", "Second", "Third", "Fourth", "Fifth", " Target Actor ", "target actor", " "};
        const auto selected = selectActorMetadata(actors, "Films with TARGET ACTOR");
        QCOMPARE(selected.size(), 5); QCOMPARE(selected.first(), QStringLiteral("Target Actor"));
        QCOMPARE(selectActorMetadata({" ", "First", "FIRST"}, ""), QStringList{"First"});
    }
    void selectionIsBoundedDeduplicatedAndIndependentOfInputOrder() {
        Request request; request.context = "explore";
        for (int i = 0; i < 90; ++i) request.candidates.append(candidate(QString::number(i), QString("Title %1").arg(i), QString::number(i % 3), i % 7));
        request.candidates.append(request.candidates.first());
        request.candidates.append(candidate("", "Empty key", "0"));
        request.candidates.append(candidate("empty-title", " ", "0"));
        const auto first = keys(selectCandidates(request, 100));
        QCOMPARE(first.size(), 30); QCOMPARE(QSet<QString>(first.begin(), first.end()).size(), 30);
        // The duplicate is identical: identity conflicts remain the authorized snapshot provider's responsibility.
        std::reverse(request.candidates.begin(), request.candidates.end());
        QCOMPARE(keys(selectCandidates(request, 100)), first);
        QVERIFY(selectCandidates(request, 0).isEmpty()); QVERIFY(selectCandidates(request, -1).isEmpty());
        QVERIFY(selectCandidates(Request{}).isEmpty());
    }
    void queryIsNotReplacedByAnExistingTargetName() {
        warehouse_contract::WarehouseRecommendationRequest query{"Pygmalion", 99, 50};
        warehouse_contract::WarehouseDetail source;
        source.warehouse = {1, "Unrelated warehouse title", 1, warehouse_contract::Visibility::Public};
        source.items = {{1, candidate("unused", "Pygmalion", "Movie").media}};
        const auto request = fromWarehouses(query, {source});
        QCOMPARE(request.context, query.query); QCOMPARE(request.limit, 50);
        query.targetWarehouseId.reset();
        QCOMPARE(fromWarehouses(query, {source}).candidates.size(), 1);
    }
    void networkResultKeepsMessageOrderAndFreshPermissionFiltering() {
        warehouse_contract::WarehouseDetail source;
        source.warehouse = {1, "source", 1, warehouse_contract::Visibility::Public};
        source.items = {{1, candidate("unused", "One", "Movie").media}, {2, candidate("unused", "Two", "Music").media}};
        source.items[1].media.id = "original-local-id";
        const Result raw{true, {{"1:2", "Reason"}, {"unknown", "Ignore"}}, "Fallback message"};
        auto output = toRecommendationResult(raw, {source});
        QVERIFY(output.fallback); QCOMPARE(output.message, raw.message);
        QCOMPARE(output.items.size(), 1); QCOMPARE(output.items[0].itemId, qint64(2));
        QCOMPARE(output.items[0].media.id, QStringLiteral("original-local-id"));
        source.items.clear(); QVERIFY(toRecommendationResult(raw, {source}).items.isEmpty());
        const auto empty = toRecommendationResult({false, {}, "No match"}, {});
        QVERIFY(!empty.fallback); QVERIFY(empty.items.isEmpty()); QCOMPARE(empty.message, QStringLiteral("No match"));
    }
};
QTEST_GUILESS_MAIN(RecallTests)
#include "llm_candidate_recall_tests.moc"
