#include "../follow_center.h"
#include "../footprint_page.h"
#include "../temporal_recommendation_page.h"

#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTabWidget>
#include <QtTest>
#include <memory>

using namespace care_contract;

namespace {

template<class T> Result<T> success(T value) { return {{}, std::move(value)}; }

ResourceSummary work(const QString& title)
{
    ResourceSummary result;
    result.media.fileName = title;
    return result;
}

PersonUpdate update(const QString& title, UpdateKind kind = UpdateKind::News)
{
    PersonUpdate result;
    result.title = title;
    result.kind = kind;
    result.sourceUrl = QUrl(QStringLiteral("https://example.test/source"));
    result.publishedAtUtc = 1700000000;
    if (kind == UpdateKind::Release)
        result.eventAtUtc = 1700000000;
    result.fetchedAtUtc = 1700000001;
    return result;
}

QPushButton* button(QWidget& page, const QString& text)
{
    for (auto* candidate : page.findChildren<QPushButton*>())
        if (candidate->text() == text)
            return candidate;
    return nullptr;
}

// A deterministic UI seam, not an implementation of the unfinished R05 service.
class FakeCareService final : public CareService {
public:
    struct WorksCall { QString person; PageRequest page; Callback<Page<ResourceSummary>> done; };
    struct UpdatesCall { PersonUpdatesRequest request; Callback<Page<PersonUpdate>> done; };
    QList<WorksCall> worksCalls;
    QList<UpdatesCall> updatesCalls;
    Callback<TemporalRecommendationResult> recommendationDone;
    BrowseScene lastScene = BrowseScene::Neutral;
    Callback<FootprintReport> footprintDone;
    FootprintRequest footprintRequest;
    QStringList cancelledHandles;
    std::function<void()> afterCancellation;
    bool immediateRecommendationFailure = false;

    void recordBehavior(const BehaviorEvent&, StatusCallback done) override { done({}); }
    void getParticipationSettings(Callback<ParticipationSettings> done) override { done(success(ParticipationSettings{})); }
    void setParticipationSettings(const ParticipationSettings&, StatusCallback done) override { done({}); }
    void clearHistory(StatusCallback done) override { done({}); }
    void getResource(const WarehouseItemRef&, Callback<ResourceSummary> done) override { done(success(ResourceSummary{})); }
    void searchPeople(const PersonSearchRequest&, Callback<Page<PersonSummary>> done) override { listFollows({}, std::move(done)); }
    void listWorkFeedback(const PageRequest&, Callback<Page<WorkFeedback>> done) override { done(success(Page<WorkFeedback>{})); }
    void setWorkFeedback(const WorkFeedbackRequest&, StatusCallback done) override { done({}); }
    void listFollows(const PageRequest&, Callback<Page<PersonSummary>> done) override
    {
        Page<PersonSummary> page;
        page.items = {{QStringLiteral("person-a"), QStringLiteral("人物甲"), {}, true},
                      {QStringLiteral("person-b"), QStringLiteral("人物乙"), {}, true}};
        done(success(std::move(page)));
    }
    void setFollow(const QString&, bool, StatusCallback done) override { done({}); }
    void getPersonWorks(const QString& person, const PageRequest& page, Callback<Page<ResourceSummary>> done) override
    { worksCalls.append({person, page, std::move(done)}); }
    void getPersonUpdates(const PersonUpdatesRequest& request, Callback<Page<PersonUpdate>> done) override
    { updatesCalls.append({request, std::move(done)}); }
    void getTrending(const TrendingRequest&, Callback<TrendingResult> done) override { done(success(TrendingResult{})); }
    void getFootprint(const FootprintRequest& request, Callback<FootprintReport> done) override
    { footprintRequest = request; footprintDone = std::move(done); }
    warehouse_contract::RecommendationHandle recommendTemporal(
        const TemporalRecommendationRequest& request, Callback<TemporalRecommendationResult> done) override
    {
        lastScene = request.scene;
        if (immediateRecommendationFailure) {
            done({{warehouse_contract::ErrorCode::NotAuthenticated, QStringLiteral("请登录")}, std::nullopt});
            return {};
        }
        recommendationDone = std::move(done);
        return QStringLiteral("pending-temporal");
    }
    bool cancelRecommendation(const warehouse_contract::RecommendationHandle& handle) override
    {
        cancelledHandles.append(handle);
        auto done = std::exchange(recommendationDone, {});
        if (done)
            done({{warehouse_contract::ErrorCode::Cancelled, QStringLiteral("取消回调不应重入页面")}, std::nullopt});
        if (afterCancellation)
            afterCancellation();
        return bool(done);
    }

    void finishWorks(int index, Page<ResourceSummary> page)
    {
        auto done = std::move(worksCalls[index].done);
        done(success(std::move(page)));
    }
    void finishUpdates(int index, Page<PersonUpdate> page)
    {
        auto done = std::move(updatesCalls[index].done);
        done(success(std::move(page)));
    }
};

} // namespace

class CarePagesTests final : public QObject {
    Q_OBJECT
private slots:
    void worksSecondPageIsReachable()
    {
        FakeCareService service;
        std::unique_ptr<CarePage> page(createFollowCenter(service));
        page->activate();
        QCOMPARE(service.worksCalls.size(), 1);
        Page<ResourceSummary> first;
        for (int i = 0; i < 50; ++i)
            first.items.append(work(QStringLiteral("作品%1").arg(i)));
        first.nextOffset = 50;
        service.finishWorks(0, std::move(first));
        auto* more = button(*page, QStringLiteral("加载更多作品"));
        QVERIFY2(more, "The works list must expose its second page");
        QVERIFY(more->isEnabled());
        more->click();
        QCOMPARE(service.worksCalls.size(), 2);
        QCOMPARE(service.worksCalls.at(1).page.offset, 50);
        QVERIFY(!more->isEnabled());
        service.finishWorks(1, {{work(QStringLiteral("第51部作品"))}, std::nullopt});
        auto* list = qobject_cast<QListWidget*>(page->findChild<QTabWidget*>()->widget(0));
        QVERIFY(list);
        QCOMPARE(list->count(), 51);
        QCOMPARE(list->item(50)->text(), QStringLiteral("第51部作品"));
        QVERIFY(!more->isEnabled());
    }

    void updatesSecondPageIsReachable()
    {
        FakeCareService service;
        std::unique_ptr<CarePage> page(createFollowCenter(service));
        page->activate();
        Page<PersonUpdate> first;
        for (int i = 0; i < 50; ++i)
            first.items.append(update(QStringLiteral("资讯%1").arg(i)));
        first.nextOffset = 50;
        service.finishUpdates(0, std::move(first));
        auto* more = button(*page, QStringLiteral("加载更多更新"));
        QVERIFY2(more, "News and release updates must expose their second page");
        QVERIFY(more->isEnabled());
        more->click();
        QCOMPARE(service.updatesCalls.size(), 2);
        QCOMPARE(service.updatesCalls.at(1).request.page.offset, 50);
        QVERIFY(!more->isEnabled());
        service.finishUpdates(1, {{update(QStringLiteral("第二页发布"), UpdateKind::Release)}, std::nullopt});
        auto* calendar = qobject_cast<QListWidget*>(page->findChild<QTabWidget*>()->widget(2));
        QVERIFY(calendar);
        QCOMPARE(calendar->count(), 1); // No leftover "no releases" placeholder.
        QVERIFY(calendar->item(0)->text().contains(QStringLiteral("第二页发布")));
        QVERIFY(!more->isEnabled());
    }

    void switchingPersonDiscardsBothPendingSecondPages()
    {
        FakeCareService service;
        std::unique_ptr<CarePage> page(createFollowCenter(service));
        page->activate();
        service.finishWorks(0, {{work(QStringLiteral("甲作品"))}, 50});
        service.finishUpdates(0, {{update(QStringLiteral("甲资讯"))}, 50});
        auto* worksMore = button(*page, QStringLiteral("加载更多作品"));
        auto* updatesMore = button(*page, QStringLiteral("加载更多更新"));
        QVERIFY(worksMore);
        QVERIFY(updatesMore);
        worksMore->click();
        updatesMore->click();
        auto* people = page->findChildren<QListWidget*>().first();
        for (auto* list : page->findChildren<QListWidget*>())
            if (list->accessibleName() == QStringLiteral("人物列表"))
                people = list;
        people->setCurrentRow(1);
        QCOMPARE(service.worksCalls.size(), 3);
        QCOMPARE(service.updatesCalls.size(), 3);
        QCOMPARE(service.worksCalls.at(2).person, QStringLiteral("person-b"));
        service.finishWorks(1, {{work(QStringLiteral("迟到甲作品"))}, 100});
        service.finishUpdates(1, {{update(QStringLiteral("迟到甲资讯"))}, 100});
        service.finishWorks(2, {{work(QStringLiteral("乙作品"))}, std::nullopt});
        service.finishUpdates(2, {{update(QStringLiteral("乙资讯"))}, std::nullopt});
        auto* tabs = page->findChild<QTabWidget*>();
        auto* works = qobject_cast<QListWidget*>(tabs->widget(0));
        auto* news = qobject_cast<QListWidget*>(tabs->widget(1));
        QCOMPARE(works->count(), 1);
        QCOMPARE(works->item(0)->text(), QStringLiteral("乙作品"));
        QCOMPARE(news->count(), 1);
        QVERIFY(news->item(0)->text().contains(QStringLiteral("乙资讯")));
        QVERIFY(!worksMore->isEnabled());
        QVERIFY(!updatesMore->isEnabled());
    }

    void temporalRequestsUseCurrentScene()
    {
        FakeCareService service;
        auto selected = BrowseScene::Relax;
        std::unique_ptr<CarePage> page(createTemporalPage(service, [&] { return selected; }));
        page->activate();
        QCOMPARE(service.lastScene, BrowseScene::Relax);
        page->deactivate();
        selected = BrowseScene::Explore;
        page->activate();
        QCOMPARE(service.lastScene, BrowseScene::Explore);
    }

    void destructionCancelsWithoutSynchronousCallbackReentry()
    {
        FakeCareService service;
        auto* page = createTemporalPage(service, [] { return BrowseScene::Neutral; });
        page->activate();
        auto* status = page->findChild<QLabel*>(QStringLiteral("careStatus"));
        QVERIFY(status);
        const auto pendingText = status->text();
        QString textDuringCancellation;
        service.afterCancellation = [&] { textDuringCancellation = status->text(); };
        delete page;
        QCOMPARE(service.cancelledHandles, QStringList{QStringLiteral("pending-temporal")});
        QCOMPARE(textDuringCancellation, pendingText);
    }

    void deactivationThenDestructionCancelsOnlyOnce()
    {
        FakeCareService service;
        auto* page = createTemporalPage(service, [] { return BrowseScene::Neutral; });
        page->activate();
        page->deactivate();
        delete page;
        QCOMPARE(service.cancelledHandles.size(), 1);
    }

    void synchronousFailureDoesNotRetainAHandle()
    {
        FakeCareService service;
        service.immediateRecommendationFailure = true;
        std::unique_ptr<CarePage> page(createTemporalPage(service, [] { return BrowseScene::Neutral; }));
        page->activate();
        QVERIFY(button(*page, QStringLiteral("生成推荐"))->isEnabled());
        QVERIFY(!button(*page, QStringLiteral("取消"))->isEnabled());
        page.reset();
        QVERIFY(service.cancelledHandles.isEmpty());
    }

    void comparisonShowsItsActualDateRange()
    {
        FakeCareService service;
        std::unique_ptr<CarePage> page(createFootprintPage(service));
        page->activate();
        QVERIFY(bool(service.footprintDone));
        FootprintReport report;
        report.current.range = service.footprintRequest.range;
        PeriodFootprint previous;
        previous.range = {care_ui::midnight(QDate(2026, 2, 1)), care_ui::midnight(QDate(2026, 3, 1))};
        previous.views = 28;
        report.comparison = previous;
        service.footprintDone(success(std::move(report)));
        bool found = false;
        for (auto* label : page->findChildren<QLabel*>())
            if (label->text().contains(QStringLiteral("前一自然区间")))
                found = label->text().contains(QStringLiteral("2026-02-01"))
                     && label->text().contains(QStringLiteral("2026-02-28"));
        QVERIFY2(found, "Raw comparison counts must identify the actual inclusive local-date range");
    }
};

QTEST_MAIN(CarePagesTests)
#include "care_pages_tests.moc"
