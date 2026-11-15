#include "care_dashboard.h"
#include "follow_center.h"
#include "footprint_page.h"
#include "temporal_recommendation_page.h"
#include "trending_page.h"

#include <QDialogButtonBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>
#include <array>

using namespace care_contract;

namespace {

template<class T>
void reply(Callback<T> done, T data)
{
    done({{}, std::move(data)});
}

class DemoCareService final : public QObject, public CareService {
public:
    DemoCareService()
    {
        person_ = {QStringLiteral("demo:asquith"), QStringLiteral("Anthony Asquith"), {PersonRole::Director}, false};
        MediaRecord record;
        record.id = QStringLiteral("uci:AA13");
        record.fileName = QStringLiteral("Pygmalion");
        record.director = person_.name;
        record.category = QStringLiteral("电影");
        work_ = {{record.id, {1, 1}}, record, {person_}};
    }

    void recordBehavior(const BehaviorEvent&, StatusCallback done) override { done({}); }
    void getParticipationSettings(Callback<ParticipationSettings> done) override
    { reply(std::move(done), settings_); }
    void setParticipationSettings(const ParticipationSettings& settings, StatusCallback done) override
    { settings_ = settings; done({}); }
    void clearHistory(StatusCallback done) override { historyCleared_ = true; done({}); }
    void getResource(const WarehouseItemRef&, Callback<ResourceSummary> done) override { reply(std::move(done), work_); }

    void searchPeople(const PersonSearchRequest& request, Callback<Page<PersonSummary>> done) override
    {
        Page<PersonSummary> page;
        if (!request.page.offset && person_.name.contains(request.query, Qt::CaseInsensitive))
            page.items << person_;
        reply(std::move(done), page);
    }

    void listFollows(const PageRequest& request, Callback<Page<PersonSummary>> done) override
    {
        Page<PersonSummary> page;
        if (!request.offset && person_.followed)
            page.items << person_;
        reply(std::move(done), page);
    }

    void setFollow(const QString&, bool followed, StatusCallback done) override
    { person_.followed = followed; done({}); }

    void getPersonWorks(const QString&, const PageRequest& request, Callback<Page<ResourceSummary>> done) override
    {
        Page<ResourceSummary> page;
        if (!request.offset)
            page.items << work_;
        reply(std::move(done), page);
    }

    void getPersonUpdates(const PersonUpdatesRequest&, Callback<Page<PersonUpdate>> done) override
    {
        // Real publication dates and source URLs are supplied by the online service.
        reply(std::move(done), Page<PersonUpdate>{});
    }

    void getTrending(const TrendingRequest& request, Callback<TrendingResult> done) override
    {
        TrendingResult result;
        result.kind = request.kind;
        result.range = request.range;
        result.generatedAtUtc = QDateTime::currentSecsSinceEpoch();
        result.containsSyntheticData = true;
        if (request.kind == TrendingKind::Works)
            result.items << TrendingItem{work_, {}, 12, 18};
        else
            result.items << TrendingItem{std::nullopt, QStringLiteral("经典电影"), 9, 13};
        reply(std::move(done), result);
    }

    void getFootprint(const FootprintRequest& request, Callback<FootprintReport> done) override
    {
        FootprintReport report;
        report.current = period(request.range, 3, 2);
        if (request.comparison)
            report.comparison = period(*request.comparison, 2, 1);
        report.generatedAtUtc = QDateTime::currentSecsSinceEpoch();
        report.containsSyntheticData = true;
        reply(std::move(done), report);
    }

    void listWorkFeedback(const PageRequest& request, Callback<Page<WorkFeedback>> done) override
    {
        Page<WorkFeedback> page;
        if (!request.offset && excluded_)
            page.items << WorkFeedback{work_, WorkFeedbackKind::NotInterested};
        reply(std::move(done), page);
    }

    void setWorkFeedback(const WorkFeedbackRequest& request, StatusCallback done) override
    {
        excluded_ = request.feedback == WorkFeedbackKind::NotInterested;
        done({});
    }

    warehouse_contract::RecommendationHandle recommendTemporal(
        const TemporalRecommendationRequest& request, Callback<TemporalRecommendationResult> done) override
    {
        const auto handle = QString::number(++next_);
        callbacks_.insert(handle, std::move(done));
        QTimer::singleShot(250, this, [this, handle, request] {
            auto callback = callbacks_.take(handle);
            if (!callback)
                return;
            TemporalRecommendationResult result;
            result.generatedAtUtc = QDateTime::currentSecsSinceEpoch();
            result.containsSyntheticData = true;
            using recommendation_contract::PreferenceMode;
            const bool categoryMatches = request.category.mode != PreferenceMode::Explicit ||
                                         request.category.value == work_.media.category;
            if (!excluded_ && categoryMatches && request.fileType.mode != PreferenceMode::Explicit)
                result.items << RecommendedResource{work_, QStringLiteral("演示理由：可查看这部影片的媒体资料。")};
            result.message = result.items.isEmpty() ? QStringLiteral("没有匹配作品。") : QStringLiteral("演示推荐");
            reply(std::move(callback), result);
        });
        return handle;
    }

    bool cancelRecommendation(const warehouse_contract::RecommendationHandle& handle) override
    {
        auto callback = callbacks_.take(handle);
        if (!callback)
            return false;
        callback({{warehouse_contract::ErrorCode::Cancelled, QStringLiteral("已取消。")}, std::nullopt});
        return true;
    }

private:
    PeriodFootprint period(TimeRange range, qint64 views, qint64 searches) const
    {
        PeriodFootprint result;
        result.range = range;
        if (historyCleared_)
            return result;
        result.coverageStartUtc = range.beginUtc;
        result.views = views;
        result.searches = searches;
        result.uniqueWorks = 1;
        result.activeDays = 1;
        result.categories << CountBucket{QStringLiteral("movie"), work_.media.category, views};
        result.people << PersonCount{person_.personId, person_.name, PersonRole::Director, views};
        for (auto day = care_ui::localTime(range.beginUtc).date();
             day <= care_ui::localTime(range.endUtc - 1).date(); day = day.addDays(1)) {
            const bool first = care_ui::midnight(day) == range.beginUtc;
            result.days << DailyActivity{day.toString(Qt::ISODate), first ? views : 0, first ? searches : 0};
        }
        return result;
    }

    ParticipationSettings settings_;
    PersonSummary person_;
    ResourceSummary work_;
    bool excluded_ = false;
    bool historyCleared_ = false;
    quint64 next_ = 0;
    QHash<QString, Callback<TemporalRecommendationResult>> callbacks_;
};

} // namespace

CareDashboard::CareDashboard(CareService& service, std::function<BrowseScene()> scene,
                             bool demonstration, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(demonstration ? QStringLiteral("关怀功能演示") : QStringLiteral("关怀服务"));
    resize(920, 630);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 16, 18, 16);
    if (demonstration) {
        auto* notice = new QLabel(QStringLiteral("模拟数据 · 此窗口中的关注、反馈和历史清理只影响本次演示。"));
        notice->setWordWrap(true);
        notice->setObjectName(QStringLiteral("resultCount"));
        layout->addWidget(notice);
    }

    auto* tabs = new QTabWidget;
    tabs->setObjectName(QStringLiteral("careDashboardTabs"));
    layout->addWidget(tabs, 1);
    const std::array<CarePage*, 4> pages{createFollowCenter(service), createTrendingPage(service),
        createFootprintPage(service), createTemporalPage(service, std::move(scene))};
    tabs->addTab(pages[0], QStringLiteral("关注中心"));
    tabs->addTab(pages[1], QStringLiteral("全站热榜"));
    tabs->addTab(pages[2], QStringLiteral("个人足迹"));
    tabs->addTab(pages[3], QStringLiteral("动态推荐"));
    connect(tabs, &QTabWidget::currentChanged, this, [pages, index = 0](int next) mutable {
        pages.at(index)->deactivate();
        index = next;
        pages.at(index)->activate();
    });
    connect(this, &QDialog::finished, this, [tabs, pages] { pages.at(tabs->currentIndex())->deactivate(); });
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("关闭"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    pages.front()->activate();
}

void showCareDemonstration(QWidget* parent, BrowseScene scene)
{
    DemoCareService service;
    CareDashboard dashboard(service, [scene] { return scene; }, true, parent);
    dashboard.exec();
}
