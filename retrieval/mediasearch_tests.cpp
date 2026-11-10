#include "mediasearch.h"

#include <QtTest>
#include <stdexcept>

namespace {

// 构造一条测试记录，id 唯一，actors 是逐个演员的姓名。
MediaRecord record(const QString& id, const QString& director,
                   const QStringList& actors, const QString& type) {
    MediaRecord media;
    media.id = id;
    media.fileName = QStringLiteral("同名作品");
    media.director = director;
    media.actors = actors;
    media.fileType = type;
    return media;
}

// 故意打乱 ID 顺序，用交叉的人名和类型制造可手算的结果。
QList<MediaRecord> sampleRecords() {
    return {
        record("M003", QStringLiteral("导演甲"), {QStringLiteral("演员乙"), QStringLiteral("演员乙")}, "MKV"),
        record("M001", QStringLiteral("导演甲"), {QStringLiteral("演员甲"), QStringLiteral("演员乙")}, "MP4"),
        record("M002", QStringLiteral("导演乙"), {QStringLiteral("演员甲")}, "MP4"),
    };
}

}  // namespace

class MediaSearchTests : public QObject {
    Q_OBJECT

private slots:
    void scanFindsExactMatches() {
        MediaSearch search;
        search.rebuild(sampleRecords());

        QCOMPARE(search.search({QStringLiteral(" 导演甲 "), {}, {}}, SearchStrategy::LinearScan),
                 (QStringList{"M001", "M003"}));
        QCOMPARE(search.search({{}, QStringLiteral("演员乙"), {}}, SearchStrategy::LinearScan),
                 (QStringList{"M001", "M003"}));
        QVERIFY(search.search({{}, QStringLiteral("演员"), {}}, SearchStrategy::LinearScan).isEmpty());
    }

    void scanCombinesConditions() {
        MediaSearch search;
        search.rebuild(sampleRecords());

        QCOMPARE(search.search({QStringLiteral("导演甲"), QStringLiteral("演员乙"), " mp4 "}, SearchStrategy::LinearScan),
                 (QStringList{"M001"}));
        QVERIFY(search.search({QStringLiteral("导演乙"), QStringLiteral("演员乙"), "MP4"},
                              SearchStrategy::LinearScan).isEmpty());
        QCOMPARE(search.search({" ", {}, {}}, SearchStrategy::LinearScan), (QStringList{"M001", "M002", "M003"}));
    }

    void fixedIndexMatchesScan() {
        MediaSearch search;
        search.rebuild(sampleRecords());
        const QList<SearchQuery> queries{
            {}, {QStringLiteral("导演甲"), {}, {}}, {{}, QStringLiteral("演员乙"), {}}, {{}, {}, " mp4 "},
            {QStringLiteral("导演甲"), QStringLiteral("演员乙"), "mp4"}, {QStringLiteral("不存在"), {}, "MP4"}
        };
        for (const SearchQuery& query : queries) {
            QCOMPARE(search.search(query, SearchStrategy::FixedIndexOrder),
                     search.search(query, SearchStrategy::LinearScan));
        }

        SearchTrace trace;
        const SearchQuery query{QStringLiteral("导演甲"), QStringLiteral("演员乙"), "MP4"};
        QCOMPARE(search.search(query, SearchStrategy::FixedIndexOrder, &trace), (QStringList{"M001"}));
        QCOMPARE(trace.recordsScanned, 0);
        QCOMPARE(trace.steps.size(), 3);
        QCOMPARE(trace.steps[0].field, SearchField::Director);
        QCOMPARE(trace.steps[1].field, SearchField::Actor);
        QCOMPARE(trace.steps[2].field, SearchField::FileType);
        QCOMPARE(trace.steps[2].remainingCount, 1);
        QCOMPARE(search.search(query), (QStringList{"M001"}));
    }

    void indexFollowsAddRemoveAndRebuild() {
        MediaSearch search;
        search.rebuild(sampleRecords());
        const SearchQuery actorQuery{{}, QStringLiteral("演员乙"), {}};
        const SearchQuery combined{QStringLiteral("导演甲"), QStringLiteral("演员乙"), "MP4"};

        search.addRecord(record("M000", QStringLiteral(" 导演甲 "),
                                {QStringLiteral(" 演员乙 "), QStringLiteral("演员乙")}, " mp4 "));
        QCOMPARE(search.search(combined), (QStringList{"M000", "M001"}));
        QCOMPARE(search.search(actorQuery), (QStringList{"M000", "M001", "M003"}));
        search.removeRecord("M001");
        QCOMPARE(search.search(combined), (QStringList{"M000"}));
        QCOMPARE(search.search({{}, QStringLiteral("演员甲"), {}}), (QStringList{"M002"}));
        QCOMPARE(search.search(actorQuery), search.search(actorQuery, SearchStrategy::LinearScan));
        search.removeRecord("M003");
        QVERIFY(search.search({{}, {}, "MKV"}).isEmpty());

        search.rebuild(sampleRecords());
        QCOMPARE(search.search(combined), (QStringList{"M001"}));
        QCOMPARE(search.search(actorQuery), (QStringList{"M001", "M003"}));
    }

    void invalidUpdatesDoNotChangeResults() {
        MediaSearch search;
        const QList<MediaRecord> records = sampleRecords();
        search.rebuild(records);

        QVERIFY_EXCEPTION_THROWN(search.addRecord(records.first()), std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(search.addRecord(MediaRecord{}), std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(search.removeRecord("missing"), std::out_of_range);
        QVERIFY_EXCEPTION_THROWN(search.rebuild({records.first(), records.first()}), std::invalid_argument);
        QCOMPARE(search.search({}), (QStringList{"M001", "M002", "M003"}));
    }

    void shortestListReducesInitialCandidates() {
        MediaSearch search;
        search.rebuild({record("M001", "D", {"Rare"}, "MP4"), record("M002", "D", {"Common"}, "MP4"),
                        record("M003", "D", {"Common"}, "MKV"), record("M004", "Other", {"Common"}, "MP4")});
        const SearchQuery query{"D", "Rare", "MP4"};
        SearchTrace fixed;
        SearchTrace optimized;

        const QStringList expected{"M001"};
        QCOMPARE(search.search(query, SearchStrategy::LinearScan), expected);
        QCOMPARE(search.search(query, SearchStrategy::FixedIndexOrder, &fixed), expected);
        QCOMPARE(search.search(query, SearchStrategy::SmallestListFirst, &optimized), expected);
        QCOMPARE(fixed.steps.first().field, SearchField::Director);
        QCOMPARE(fixed.steps.first().remainingCount, 3);
        QCOMPARE(optimized.steps.first().field, SearchField::Actor);
        QCOMPARE(optimized.steps.first().remainingCount, 1);
    }

    void missingConditionStopsEarly() {
        MediaSearch search;
        search.rebuild(sampleRecords());
        SearchTrace trace;
        const SearchQuery query{QStringLiteral("导演甲"), QStringLiteral("不存在的演员"), "MP4"};
        QVERIFY(search.search(query, SearchStrategy::SmallestListFirst, &trace).isEmpty());
        QCOMPARE(trace.steps.size(), 1);
        QCOMPARE(trace.steps.first().field, SearchField::Actor);
        QCOMPARE(trace.steps.first().remainingCount, 0);
    }
};

QTEST_GUILESS_MAIN(MediaSearchTests)
#include "mediasearch_tests.moc"
