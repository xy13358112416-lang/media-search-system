#include "personalrecommendation_service.h"
#include "mediarecommendation.h"
#include <QtTest>
#include <limits>

using namespace account_contract;
using namespace recommendation_contract;

namespace {
// Only session/history are used by recommendation. Real SQLite is tested separately.
class Accounts final : public AccountHistoryService {
public:
    std::optional<UserIdentity> user = UserIdentity{1, "one", UserRole::User};
    QHash<qint64, QList<MediaHistoryEntry>> histories;
    Status historyStatus;
    bool missingValue = false;
    int reads = 0;
    std::optional<UserIdentity> currentUser() const override { return user; }
    Result<QList<MediaHistoryEntry>> loadMediaHistory(qint64 id) override {
        ++reads;
        if (!historyStatus.ok()) return {historyStatus, {}};
        if (missingValue) return {};
        return {{}, histories.value(id)};
    }
    void logout() override { user.reset(); }
    Status initializeDatabase(const QString&) override { return {}; }
    Result<bool> needsInitialAdmin() override { return {{}, false}; }
    Status createInitialAdmin(const QString&, const QString&) override { return {}; }
    Status registerUser(const QString&, const QString&) override { return {}; }
    Result<UserIdentity> login(const QString&, const QString&) override { return {{}, user}; }
    MediaMutationResult addMedia(const MediaRecord&) override { return {}; }
    MediaMutationResult removeMedia(const QString&) override { return {}; }
    Status recordMediaView(qint64, const QString&) override { return {}; }
};
QList<MediaRecord> catalogue() {
    QList<MediaRecord> records;
    for (const auto& values : QList<QStringList>{{"M1", "音乐", "MP3"}, {"M2", "音乐", "WAV"},
                                                {"M3", "电影", "MP4"}}) {
        MediaRecord record;
        record.id = values[0]; record.category = values[1]; record.fileType = values[2];
        record.viewCount = record.id == "M3" ? 100 : record.id == "M2" ? 20 : 10;
        records.append(record);
    }
    return records;
}
}

class PersonalRecommendationTests : public QObject {
    Q_OBJECT
private slots:
    void autoAggregatesClicksAndUsersStaySeparate() {
        Accounts accounts;
        auto records = catalogue();
        accounts.histories[1] = {{"M1", 4, 1}, {"M2", 3, 2}, {"M3", 5, 3}};
        accounts.histories[2] = {{"M3", 9, 4}};
        PersonalRecommendationServiceImpl service(accounts, [&] { return records; });
        auto first = service.recommend({1});
        QVERIFY(first.success);
        // Category totals: music=7, film=5; type totals: MP4=5, MP3=4, WAV=3.
        QCOMPARE(first.mediaIds, recommendMedia(records, {QStringLiteral("音乐"), "MP4", 5}));
        QVERIFY(first.reason.contains(QStringLiteral("音乐")));
        accounts.user = UserIdentity{2, "two", UserRole::User};
        auto second = service.recommend({2});
        QVERIFY(second.success);
        QCOMPARE(second.mediaIds.first(), QStringLiteral("M3"));
        QVERIFY(second.mediaIds != first.mediaIds);
        QVERIFY(!service.recommend({1}).success);
        accounts.logout();
        QVERIFY(!service.recommend({2}).success);
    }
    void preferenceCombinations_data() {
        QTest::addColumn<int>("categoryMode"); QTest::addColumn<int>("typeMode");
        for (int category = 0; category < 3; ++category)
            for (int type = 0; type < 3; ++type)
                QTest::newRow(qPrintable(QString::number(category) + ":" + QString::number(type))) << category << type;
    }
    void preferenceCombinations() {
        QFETCH(int, categoryMode); QFETCH(int, typeMode);
        Accounts accounts; auto records = catalogue();
        accounts.histories[1] = {{"M1", 9, 1}};
        PersonalRecommendationServiceImpl service(accounts, [&] { return records; });
        RecommendationRequest request{1, {static_cast<PreferenceMode>(categoryMode), QStringLiteral("电影")},
                                          {static_cast<PreferenceMode>(typeMode), " wav "}, 2};
        auto result = service.recommend(request);
        QVERIFY(result.success);
        const QString category = categoryMode == 0 ? QStringLiteral("音乐") : categoryMode == 1 ? QString() : QStringLiteral("电影");
        const QString type = typeMode == 0 ? "MP3" : typeMode == 1 ? QString() : "WAV";
        QCOMPARE(result.mediaIds, recommendMedia(records, {category, type, 2}));
        QCOMPARE(accounts.reads, categoryMode == 0 || typeMode == 0 ? 1 : 0);
        QVERIFY(!result.reason.isEmpty()); QVERIFY(result.error.isEmpty());
    }
    void noHistoryAndDeletedMediaFallBackWithoutLosingExplicitChoice() {
        Accounts accounts; auto records = catalogue();
        PersonalRecommendationServiceImpl service(accounts, [&] { return records; });
        auto cold = service.recommend({1});
        QVERIFY(cold.success); QCOMPARE(cold.mediaIds, recommendMedia(records, {}));
        QVERIFY(cold.reason.contains(QStringLiteral("热度")));
        accounts.histories[1] = {{"deleted", 999, 20}, {"M1", 2, 1}};
        QCOMPARE(service.recommend({1}).mediaIds.first(), QStringLiteral("M1"));
        records.removeFirst(); // The provider supplies the current snapshot on every request.
        RecommendationRequest request{1, {}, {PreferenceMode::Explicit, " wav "}};
        auto result = service.recommend(request);
        QVERIFY(result.success);
        QCOMPARE(result.mediaIds, recommendMedia(records, {{}, "WAV", 5}));
        QVERIFY(!result.mediaIds.contains("M1"));
    }
    void tiesUseRecencyThenLexicalOrder() {
        Accounts accounts; auto records = catalogue();
        PersonalRecommendationServiceImpl service(accounts, [&] { return records; });
        accounts.histories[1] = {{"M1", 3, 1}, {"M3", 3, 2}};
        QVERIFY(service.recommend({1}).reason.contains(QStringLiteral("电影")));
        accounts.histories[1] = {{"M2", 3, 2}, {"M3", 3, 2}};
        // QString lexical order: 电影 < 音乐; MP4 < WAV.
        QCOMPARE(service.recommend({1}).mediaIds.first(), QStringLiteral("M3"));
        const RecommendationRequest typeOnly{1, {PreferenceMode::Any, {}}, {PreferenceMode::Auto, {}}};
        accounts.histories[1] = {{"M2", 3, 3}, {"M3", 3, 2}};
        QCOMPARE(service.recommend(typeOnly).mediaIds.first(), QStringLiteral("M2")); // Recent WAV beats MP4.
        accounts.histories[1] = {{"M2", 3, 2}, {"M1", 3, 2}};
        QCOMPARE(service.recommend(typeOnly).mediaIds.first(), QStringLiteral("M1")); // MP3 < WAV.
    }
    void historyFailuresAreNotColdStart() {
        Accounts accounts; auto records = catalogue();
        PersonalRecommendationServiceImpl service(accounts, [&] { return records; });
        accounts.historyStatus = {ErrorCode::DatabaseError, QStringLiteral("读取失败")};
        auto failed = service.recommend({1});
        QVERIFY(!failed.success); QVERIFY(failed.mediaIds.isEmpty()); QVERIFY(!failed.error.isEmpty());
        accounts.historyStatus = {}; accounts.missingValue = true;
        QVERIFY(!service.recommend({1}).success);
        // An all-manual request doesn't need personal history.
        QVERIFY(service.recommend({1, {PreferenceMode::Any, {}}, {PreferenceMode::Any, {}}}).success);
    }
    void emptyLimitsInvalidRequestsAndProviders() {
        Accounts accounts; auto records = catalogue();
        PersonalRecommendationServiceImpl service(accounts, [&] { return records; });
        QVERIFY(!service.recommend({0}).success);
        QVERIFY(!service.recommend({1, {PreferenceMode::Explicit, " "}}).success);
        QVERIFY(!service.recommend({1, {static_cast<PreferenceMode>(99), {}}}).success);
        auto empty = service.recommend({1, {}, {}, 0});
        QVERIFY(empty.success); QVERIFY(empty.mediaIds.isEmpty()); QCOMPARE(accounts.reads, 0);
        records.clear(); QVERIFY(service.recommend({1}).success);
        PersonalRecommendationServiceImpl unavailable(accounts, {});
        QVERIFY(!unavailable.recommend({1}).success);
    }
    void invalidAndOverflowingHistoryIsRejected() {
        Accounts accounts; auto records = catalogue();
        PersonalRecommendationServiceImpl service(accounts, [&] { return records; });
        accounts.histories[1] = {{"M1", -1, 1}};
        QVERIFY(!service.recommend({1}).success);
        const auto large = std::numeric_limits<qint64>::max();
        records.append(records.first()); records.last().id = "M4";
        accounts.histories[1] = {{"M1", large, 1}, {"M2", large, 2}, {"M4", large, 3}};
        QVERIFY(!service.recommend({1}).success);
    }
};
QTEST_GUILESS_MAIN(PersonalRecommendationTests)
#include "personalrecommendation_tests.moc"
