#include "todayrecommendation_service.h"
#include "daily_pick.h"
#include "mediarecommendation.h"
#include <QDate>
#include <QtTest>

using namespace recommendation_contract;

namespace {

// 可编程的 PersonalRecommendationService 假实现，用于测试今日推荐的组合逻辑。
class Base final : public PersonalRecommendationService {
public:
    RecommendationResult result;
    RecommendationRequest lastRequest;
    RecommendationResult recommend(const RecommendationRequest& request) override {
        lastRequest = request;
        return result;
    }
};

QStringList ids(int n) {
    QStringList out;
    for (int i = 1; i <= n; ++i) {
        out.append(QStringLiteral("M%1").arg(i));
    }
    return out;
}

} // namespace

class TodayRecommendationTests : public QObject {
    Q_OBJECT
private slots:
    void dailyPickIsDeterministicAndRespectsLimit() {
        const QStringList pool = ids(12);
        const auto first = dailyPick(pool, 42, 5);
        const auto second = dailyPick(pool, 42, 5);
        QCOMPARE(first, second);
        QCOMPARE(first.size(), 5);
        for (const QString& id : first) {
            QVERIFY(pool.contains(id));
        }
        const auto full = dailyPick(pool, 7, 12);
        QCOMPARE(full.size(), pool.size());
        QStringList sortedFull = full;
        QStringList sortedPool = pool;
        sortedFull.sort();
        sortedPool.sort();
        QCOMPARE(sortedFull, sortedPool); // 全量取时是输入的一个排列。
    }

    void dailyPickVariesBySeed() {
        const QStringList pool = ids(20);
        const QStringList first = dailyPick(pool, 1, 20);
        bool varied = false;
        for (quint64 seed = 2; seed <= 40 && !varied; ++seed) {
            varied = dailyPick(pool, seed, 20) != first;
        }
        QVERIFY(varied);
    }

    void forwardsRequestAndPicksFromPool() {
        Base base;
        base.result = RecommendationResult{true, {}, ids(30), QStringLiteral("依据说明")};
        TodayRecommendationServiceImpl service(base, {});
        TodayRecommendationRequest request;
        request.userId = 7;
        request.category = {PreferenceMode::Explicit, QStringLiteral("电影")};
        request.fileType = {PreferenceMode::Any, {}};
        request.limit = 6;
        request.seed = 42;
        const auto result = service.recommendToday(request);
        QVERIFY(result.success);
        QCOMPARE(base.lastRequest.userId, qint64(7));
        QCOMPARE(base.lastRequest.category.mode, PreferenceMode::Explicit);
        QCOMPARE(base.lastRequest.limit, 30); // 先取候选池。
        QCOMPARE(result.mediaIds.size(), 6);
        for (const QString& id : result.mediaIds) {
            QVERIFY(base.result.mediaIds.contains(id));
        }
        QVERIFY(!result.dateKey.isEmpty());
        QVERIFY(result.reason.contains(QStringLiteral("依据说明")));
    }

    void seedZeroUsesToday() {
        Base base;
        base.result = RecommendationResult{true, {}, ids(30), {}};
        TodayRecommendationServiceImpl service(base, {});
        const auto byZero = service.recommendToday({7, {}, {}, 6, 0});
        const quint64 today = static_cast<quint64>(QDate::currentDate().toJulianDay());
        const auto byDate = service.recommendToday({7, {}, {}, 6, today});
        QCOMPARE(byZero.mediaIds, byDate.mediaIds);
    }

    void baseFailurePropagates() {
        Base base;
        base.result = RecommendationResult{false, QStringLiteral("会话无效"), {}, {}};
        TodayRecommendationServiceImpl service(base, {});
        const auto result = service.recommendToday({7});
        QVERIFY(!result.success);
        QCOMPARE(result.error, QStringLiteral("会话无效"));
        QVERIFY(result.mediaIds.isEmpty());
    }

    void emptyPoolFallsBackToHeat() {
        Base base;
        base.result = RecommendationResult{true, {}, {}, {}};
        QList<MediaRecord> records;
        for (const auto& values : QList<QStringList>{{"A", "纪录片", "MP4"}, {"B", "音乐", "MP3"}, {"C", "电影", "MP4"}}) {
            MediaRecord record;
            record.id = values[0];
            record.category = values[1];
            record.fileType = values[2];
            record.viewCount = record.id == "C" ? 100 : record.id == "B" ? 20 : 10;
            records.append(record);
        }
        TodayRecommendationServiceImpl service(base, [&] { return records; });
        const auto result = service.recommendToday({7, {}, {}, 6, 42});
        QVERIFY(result.success);
        const QStringList expected = dailyPick(recommendMedia(records, RecommendationOptions{{}, {}, 30}), 42, 6);
        QCOMPARE(result.mediaIds, expected);
    }
};

QTEST_GUILESS_MAIN(TodayRecommendationTests)
#include "todayrecommendation_tests.moc"
