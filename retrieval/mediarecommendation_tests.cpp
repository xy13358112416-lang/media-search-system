#include "mediarecommendation.h"

#include <QtTest>
#include <limits>

namespace {

// 为推荐测试设置需要参与评分的媒体字段。
MediaRecord record(const QString& id, const QString& category, const QString& fileType, quint64 views) {
    MediaRecord media;
    media.id = id;
    media.category = category;
    media.fileType = fileType;
    media.viewCount = views;
    return media;
}

QList<MediaRecord> catalogue() {
    return {
        record("M003", QStringLiteral("电影"), "MP4", 200),
        record("M002", QStringLiteral("纪录片"), "MP4", 20),
        record("M001", QStringLiteral("纪录片"), "MKV", 100),
    };
}

}  // namespace

class MediaRecommendationTests : public QObject {
    Q_OBJECT

private slots:
    void categoryPreferenceChangesRanking() {
        const QList<MediaRecord> records = catalogue();
        RecommendationOptions options;
        options.preferredCategory = QStringLiteral(" 纪录片 ");
        options.preferredFileType = "MP4";
        QCOMPARE(recommendMedia(records, options), (QStringList{"M002", "M001", "M003"}));

        options.preferredCategory = QStringLiteral("电影");
        QCOMPARE(recommendMedia(records, options), (QStringList{"M003", "M002", "M001"}));
    }

    void formatPreferenceChangesRanking() {
        const QList<MediaRecord> records = catalogue();
        RecommendationOptions options{QStringLiteral("纪录片"), " mp4 ", 5};
        QCOMPARE(recommendMedia(records, options), (QStringList{"M002", "M001", "M003"}));

        options.preferredFileType = " mkv ";
        QCOMPARE(recommendMedia(records, options), (QStringList{"M001", "M002", "M003"}));
    }

    void noPreferencesUsesExactViewCount() {
        const quint64 mostViews = std::numeric_limits<quint64>::max();
        const QList<MediaRecord> records{
            record("M003", {}, "MP4", mostViews),
            record("M001", {}, "MP4", mostViews - 1),
            record("M002", {}, "MP4", mostViews - 2),
        };

        QCOMPARE(recommendMedia(records, {}), (QStringList{"M003", "M001", "M002"}));
        QCOMPARE(recommendMedia(records, {"  ", "  ", 2}), (QStringList{"M003", "M001"}));
    }

    void zeroViewsAndEqualScoresUseIdOrder() {
        const QList<MediaRecord> records{
            record("M003", QStringLiteral("纪录片"), "MP4", 0),
            record("M001", QStringLiteral("电影"), "MP4", 0),
            record("M002", QStringLiteral("纪录片"), "MP4", 0),
        };

        QCOMPARE(recommendMedia(records, {QStringLiteral("纪录片"), "MP4", 5}),
                 (QStringList{"M002", "M003", "M001"}));
        QCOMPARE(recommendMedia(records, {}), (QStringList{"M001", "M002", "M003"}));
    }

    void equalScoresFromDifferentSignalsUseIdOrder() {
        const QList<MediaRecord> records{
            record("M002", QStringLiteral("纪录片"), "MKV", 0),
            record("M001", QStringLiteral("电影"), "MP4", 200),
        };
        // M001 得 1 + 1 分，M002 得 2 + 0 分，按 ID 打破平分。
        QCOMPARE(recommendMedia(records, {QStringLiteral("纪录片"), "MP4", 5}),
                 (QStringList{"M001", "M002"}));
    }

    void handlesEmptyCatalogueAndResultLimits() {
        auto records = catalogue();
        records.append(record("M004", {}, "MP4", 0));
        records.append(record("M005", {}, "MP4", 0));
        records.append(record("M006", {}, "MP4", 0));

        QCOMPARE(recommendMedia(records, {}), (QStringList{"M003", "M001", "M002", "M004", "M005"}));
        QVERIFY(recommendMedia({}, {}).isEmpty());
        QVERIFY(recommendMedia(records, {{}, {}, 0}).isEmpty());
        QVERIFY(recommendMedia(records, {{}, {}, -1}).isEmpty());
    }
};

QTEST_GUILESS_MAIN(MediaRecommendationTests)
#include "mediarecommendation_tests.moc"
