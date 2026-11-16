#include "../follow_center.h"
#include "../footprint_page.h"
#include "../temporal_recommendation_page.h"
#include "../trending_page.h"

#include <QComboBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
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
    struct TrendingCall { TrendingRequest request; Callback<TrendingResult> done; };
    QList<WorksCall> worksCalls;
    QList<UpdatesCall> updatesCalls;
    QList<TrendingCall> trendingCalls;
    bool immediateTrendingFailure = false;
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
    void getTrending(const TrendingRequest& request, Callback<TrendingResult> done) override
    {
        trendingCalls.append({request, std::move(done)});
        if (immediateTrendingFailure)
            failTrending(trendingCalls.size() - 1);
    }
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
    void finishTrending(int index, const QString& summary)
    {
        TrendingResult result;
        result.summary = summary;
        result.summaryFromModel = trendingCalls.at(index).request.summaryMode == TrendingSummaryMode::Model;
        TrendingItem item;
        item.term = summary;
        result.items.append(item);
        auto done = std::move(trendingCalls[index].done);
        done(success(std::move(result)));
    }
    void failTrending(int index)
    {
        auto done = std::move(trendingCalls[index].done);
        done({{warehouse_contract::ErrorCode::NotAuthenticated, QStringLiteral("请登录")}, std::nullopt});
    }
};

} // namespace

class CarePagesTests final : public QObject {
    Q_OBJECT
private slots:
    void trendingOrdinaryActionsOnlyRequestRules()
    {
        FakeCareService service;
        std::unique_ptr<CarePage> page(createTrendingPage(service));
        page->activate();
        QCOMPARE(service.trendingCalls.size(), 1);
        auto filters = page->findChildren<QComboBox*>();
        QCOMPARE(filters.size(), 2);
        filters.at(0)->setCurrentIndex(1);
        filters.at(1)->setCurrentIndex(1);
        button(*page, QStringLiteral("刷新热榜"))->click();
        QCOMPARE(service.trendingCalls.size(), 4);
        for (const auto& call : service.trendingCalls)
            QCOMPARE(call.request.summaryMode, TrendingSummaryMode::Rules);
        const auto today = care_ui::localTime(QDateTime::currentSecsSinceEpoch()).date();
        QCOMPARE(service.trendingCalls.first().request.range.beginUtc, care_ui::midnight(today));
        QCOMPARE(service.trendingCalls.at(1).request.range.beginUtc,
                 care_ui::midnight(today.addDays(1 - today.dayOfWeek())));
        QCOMPARE(service.trendingCalls.last().request.kind, TrendingKind::SearchTerms);
    }

    void trendingModelRequiresClickAndDiscardsOlderRules()
    {
        FakeCareService service;
        std::unique_ptr<CarePage> page(createTrendingPage(service));
        page->activate();
        auto* model = page->findChild<QPushButton*>(QStringLiteral("careTrendingModelSummary"));
        QVERIFY(model);
        QCOMPARE(model->text(), QStringLiteral("生成 AI 摘要"));
        model->click();
        QCOMPARE(service.trendingCalls.size(), 2);
        QCOMPARE(service.trendingCalls.at(1).request.summaryMode, TrendingSummaryMode::Model);
        QCOMPARE(service.trendingCalls.at(1).request.kind, service.trendingCalls.at(0).request.kind);
        QCOMPARE(service.trendingCalls.at(1).request.range.beginUtc, service.trendingCalls.at(0).request.range.beginUtc);
        QCOMPARE(service.trendingCalls.at(1).request.range.endUtc, service.trendingCalls.at(0).request.range.endUtc);
        QVERIFY(!model->isEnabled());
        model->click();
        QCOMPARE(service.trendingCalls.size(), 2);
        const auto modelText = QStringLiteral("<b>模型摘要</b>");
        service.finishTrending(1, modelText);
        QVERIFY(model->isEnabled());
        service.finishTrending(0, QStringLiteral("过期规则摘要"));
        auto* table = page->findChild<QTableWidget*>(QStringLiteral("careTrendingTable"));
        QCOMPARE(table->item(0, 1)->text(), modelText);
        QLabel* summary = nullptr;
        for (auto* label : page->findChildren<QLabel*>())
            if (label->text().contains(modelText))
                summary = label;
        QVERIFY(summary);
        QCOMPARE(summary->textFormat(), Qt::PlainText);
        QVERIFY(summary->text().startsWith(QStringLiteral("AI 摘要")));
    }

    void trendingModelFailureAllowsRetryIncludingSynchronousFailure()
    {
        FakeCareService service;
        std::unique_ptr<CarePage> page(createTrendingPage(service));
        page->activate();
        auto* model = page->findChild<QPushButton*>(QStringLiteral("careTrendingModelSummary"));
        QVERIFY(model);
        model->click();
        service.failTrending(1);
        QVERIFY(model->isEnabled());
        service.immediateTrendingFailure = true;
        model->click();
        QCOMPARE(service.trendingCalls.size(), 3);
        QVERIFY(model->isEnabled());
        QCOMPARE(page->findChild<QLabel*>(QStringLiteral("careStatus"))->text(), QStringLiteral("请登录"));
    }

    void trendingFilterAndRefreshInvalidatePendingModel()
    {
        FakeCareService service;
        std::unique_ptr<CarePage> page(createTrendingPage(service));
        page->activate();
        auto* model = page->findChild<QPushButton*>(QStringLiteral("careTrendingModelSummary"));
        QVERIFY(model);
        model->click();
        page->findChildren<QComboBox*>().at(1)->setCurrentIndex(1);
        QVERIFY(model->isEnabled());
        QCOMPARE(service.trendingCalls.at(2).request.summaryMode, TrendingSummaryMode::Rules);
        service.finishTrending(2, QStringLiteral("新筛选结果"));
        service.finishTrending(1, QStringLiteral("过期模型结果"));
        auto* table = page->findChild<QTableWidget*>(QStringLiteral("careTrendingTable"));
        QCOMPARE(table->item(0, 1)->text(), QStringLiteral("新筛选结果"));
        model->click();
        button(*page, QStringLiteral("刷新热榜"))->click();
        QVERIFY(model->isEnabled());
        QCOMPARE(service.trendingCalls.at(4).request.summaryMode, TrendingSummaryMode::Rules);
        service.finishTrending(4, QStringLiteral("刷新结果"));
        service.finishTrending(3, QStringLiteral("刷新前模型结果"));
        QCOMPARE(table->item(0, 1)->text(), QStringLiteral("刷新结果"));
    }

    void trendingSessionTransitionsIgnorePendingModel_data()
    {
        QTest::addColumn<bool>("resetSession");
        QTest::newRow("deactivate") << false;
        QTest::newRow("reset-session") << true;
    }

    void trendingSessionTransitionsIgnorePendingModel()
    {
        QFETCH(bool, resetSession);
        FakeCareService service;
        std::unique_ptr<CarePage> page(createTrendingPage(service));
        page->activate();
        service.finishTrending(0, QStringLiteral("旧账号结果"));
        auto* model = page->findChild<QPushButton*>(QStringLiteral("careTrendingModelSummary"));
        QVERIFY(model);
        model->click();
        if (resetSession)
            page->resetSession();
        else
            page->deactivate();
        QVERIFY(model->isEnabled());
        auto* table = page->findChild<QTableWidget*>(QStringLiteral("careTrendingTable"));
        QCOMPARE(table->rowCount(), 0);
        service.finishTrending(1, QStringLiteral("迟到旧账号模型结果"));
        QCOMPARE(table->rowCount(), 0);
        QVERIFY(page->findChild<QLabel*>(QStringLiteral("careStatus"))->text().isEmpty());
        page->activate();
        QCOMPARE(service.trendingCalls.size(), 3);
        QCOMPARE(service.trendingCalls.at(2).request.summaryMode, TrendingSummaryMode::Rules);
        QVERIFY(model->isEnabled());
        for (auto* label : page->findChildren<QLabel*>())
            QVERIFY(!label->text().contains(QStringLiteral("旧账号")));
    }

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

    void staleNewsIsIdentifiedAsCached()
    {
        FakeCareService service;
        std::unique_ptr<CarePage> page(createFollowCenter(service));
        page->activate();
        auto oldArticle = update(QStringLiteral("之前的资讯"));
        oldArticle.stale = true;
        service.finishUpdates(0, {{oldArticle}, std::nullopt});
        auto* news = qobject_cast<QListWidget*>(page->findChild<QTabWidget*>()->widget(1));
        QVERIFY(news && news->count() == 1);
        QVERIFY(news->item(0)->text().contains(QStringLiteral("之前的资讯")));
        QVERIFY(news->item(0)->text().contains(QStringLiteral("已过期缓存")));
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

    void workDetailsRequireAnExplicitActionAndDiscardLateReplies()
    {
        FakeCareService service;
        int views = 0;
        WarehouseItemRef selected;
        Callback<ResourceSummary> pending;
        std::unique_ptr<CarePage> page(createFollowCenter(service, [&](const auto& source, auto done) {
            ++views;
            selected = source;
            pending = std::move(done);
        }));
        page->activate();
        auto entry = work(QStringLiteral("作品"));
        entry.resource.source = {17, 29};
        service.finishWorks(0, {{entry}, std::nullopt});
        auto* list = qobject_cast<QListWidget*>(page->findChild<QTabWidget*>()->widget(0));
        auto* open = button(*page, QStringLiteral("查看作品详情"));
        QVERIFY(list && open && !open->isEnabled());
        list->setCurrentRow(0);
        QCOMPARE(views, 0);
        open->click();
        QCOMPARE(views, 1);
        QCOMPARE(selected.warehouseId, qint64(17));
        QCOMPARE(selected.itemId, qint64(29));
        page->resetSession();
        entry.media.fileName = QStringLiteral("迟到的详情");
        pending(success(entry));
        QVERIFY(list->count() == 0);
        for (const auto* label : page->findChildren<QLabel*>())
            QVERIFY(!label->text().contains(entry.media.fileName));
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
