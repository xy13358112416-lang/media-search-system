// >>> DEFENSE-NOTE | 头文件导读｜在线足迹、热榜、人物与推荐的共享契约
// 这些类型在Qt、网络适配和服务器之间保持一致；CareService是异步接口，身份沿用当前登录会话。
// 读法：struct说明传哪些数据；class/函数声明说明谁可调用；实际算法去同名.cpp或引用的实现文件。
// <<< DEFENSE-NOTE | 本段讲解结束
#pragma once

#include "behavior_contract.h"
#include "personalrecommendation_contract.h"
#include <QUrl>

namespace care_contract {
enum class PersonRole { Actor, Director, Musician };
enum class BrowseScene { Neutral, Relax, Focus, Explore }; // User-selected context, not inferred emotion.
enum class TrendingKind { Works, SearchTerms };
enum class TrendingSummaryMode { Rules, Model };
enum class UpdateKind { News, Release };
enum class RecommendationEngine { Temporal, DeepSeek };
enum class WorkFeedbackKind { Neutral, NotInterested };

// >>> DEFENSE-NOTE | 类型说明｜PersonSummary
// 稳定personId、展示姓名、多角色和本人关注标志；同名不代表同一人。
// <<< DEFENSE-NOTE | 本段讲解结束
struct PersonSummary {
    QString personId;
    QString name;
    QList<PersonRole> roles; // One stable person may have multiple roles; equal names do not imply equal IDs.
    bool followed = false;
};
// >>> DEFENSE-NOTE | 类型说明｜ResourceSummary
// 作品身份、媒体信息与可信人物列表的组合，是推荐和统计的候选单位。
// <<< DEFENSE-NOTE | 本段讲解结束
struct ResourceSummary { ResourceRef resource; MediaRecord media; QList<PersonSummary> people; };
// >>> DEFENSE-NOTE | 类型说明｜RecommendedResource
// 一条已排序推荐：work提供资源，reason解释选择；Qt保留服务端顺序。
// <<< DEFENSE-NOTE | 本段讲解结束
struct RecommendedResource { ResourceSummary work; QString reason; };
struct PersonSearchRequest { QString query; PageRequest page; };
// External works are reference material, never implicitly treated as readable library resources.
struct PersonWebWork { QString title; QString year; QString role; QUrl sourceUrl; };
struct PersonWebSource {
    QString title;
    QUrl url;
    QString biography;
    QList<PersonWebWork> works;
};
struct WorkFeedbackRequest { ResourceRef resource; WorkFeedbackKind feedback = WorkFeedbackKind::NotInterested; };
struct WorkFeedback { ResourceSummary work; WorkFeedbackKind feedback = WorkFeedbackKind::NotInterested; };
struct PersonUpdatesRequest { QString personId; TimeRange range; PageRequest page; };
// >>> DEFENSE-NOTE | 类型说明｜PersonUpdate
// 人物资讯：稳定updateId、文章URL、可选发表/活动时间、抓取时间与过期状态。未知活动时间不能编成日历事件。
// <<< DEFENSE-NOTE | 本段讲解结束
struct PersonUpdate {
    QString updateId;
    QString personId;
    QString title;
    UpdateKind kind = UpdateKind::News;
    QUrl sourceUrl;
    std::optional<qint64> publishedAtUtc;
    std::optional<qint64> eventAtUtc; // Unknown release dates stay absent and do not become calendar events.
    qint64 fetchedAtUtc = 0;
    bool stale = false;
};
// >>> DEFENSE-NOTE | 类型说明｜TrendingRequest
// 榜单类型、时间窗口、条数和摘要模式；默认Rules不发起付费模型。
// <<< DEFENSE-NOTE | 本段讲解结束
struct TrendingRequest {
    TrendingKind kind = TrendingKind::Works;
    TimeRange range;
    int limit = 10;
    TrendingSummaryMode summaryMode = TrendingSummaryMode::Rules;
};
// >>> DEFENSE-NOTE | 类型说明｜TrendingItem
// 作品或搜索词，加不同用户数与封顶后的有效事件数；不是原始点击次数。
// <<< DEFENSE-NOTE | 本段讲解结束
struct TrendingItem {
    std::optional<ResourceSummary> work; // Works: present and term empty. SearchTerms: absent and term set.
    QString term;
    qint64 distinctUsers = 0;
    qint64 effectiveEvents = 0; // Capped per user/object/local day, not raw clicks.
};
// >>> DEFENSE-NOTE | 类型说明｜TrendingResult
// 有序条目、生成时间、摘要、是否模型生成及模拟来源标志；模型不能改条目计数。
// <<< DEFENSE-NOTE | 本段讲解结束
struct TrendingResult {
    TrendingKind kind = TrendingKind::Works;
    TimeRange range;
    QList<TrendingItem> items;
    qint64 generatedAtUtc = 0;
    QString summary;
    bool summaryFromModel = false;
    bool containsSyntheticData = false;
};
struct CountBucket { QString key; QString label; qint64 count = 0; };
struct PersonCount { QString personId; QString name; PersonRole role = PersonRole::Actor; qint64 count = 0; };
struct DailyActivity { QString localDate; qint64 views = 0; qint64 searches = 0; }; // YYYY-MM-DD, Asia/Shanghai.
// >>> DEFENSE-NOTE | 类型说明｜PeriodFootprint
// 一个区间的浏览/搜索、不同作品、活跃日、类别/人物和日历统计；coverageStartUtc表示实际可用历史起点。
// <<< DEFENSE-NOTE | 本段讲解结束
struct PeriodFootprint {
    TimeRange range;
    std::optional<qint64> coverageStartUtc; // First actually available event; absent for no history.
    qint64 views = 0;
    qint64 searches = 0;
    qint64 uniqueWorks = 0;
    qint64 activeDays = 0;
    QList<CountBucket> categories;
    QList<PersonCount> people; // Credit each distinct (person,role) once per view; totals may exceed views.
    QList<DailyActivity> days;
};
// >>> DEFENSE-NOTE | 类型说明｜FootprintRequest
// 主区间及可选显式比较区间；算法不会自动替用户猜一个比较期。
// <<< DEFENSE-NOTE | 本段讲解结束
struct FootprintRequest {
    TimeRange range;
    std::optional<TimeRange> comparison; // Explicit earlier non-overlapping window; never inferred by the algorithm.
};
// >>> DEFENSE-NOTE | 类型说明｜FootprintReport
// 当前期与可选比较期报告；containsSyntheticData提醒演示历史参与。
// <<< DEFENSE-NOTE | 本段讲解结束
struct FootprintReport {
    PeriodFootprint current;
    std::optional<PeriodFootprint> comparison;
    qint64 generatedAtUtc = 0;
    QString message;
    bool containsSyntheticData = false;
};
// >>> DEFENSE-NOTE | 类型说明｜TemporalRecommendationRequest
// 在线请求：类别/格式三态、主动场景和条数；身份来自连接会话，不放任意userId。
// <<< DEFENSE-NOTE | 本段讲解结束
struct TemporalRecommendationRequest {
    recommendation_contract::PreferenceSelection category;
    recommendation_contract::PreferenceSelection fileType;
    BrowseScene scene = BrowseScene::Neutral;
    int limit = 5;
};
// >>> DEFENSE-NOTE | 类型说明｜TemporalRecommendationResult
// 推荐有序作品、信息、fallback、引擎、时间和来源。正常空结果与模型失败回退必须区分。
// <<< DEFENSE-NOTE | 本段讲解结束
struct TemporalRecommendationResult {
    QList<RecommendedResource> items; // Ordered, canonical identities and current readable sources included.
    QString message;
    bool fallback = false; // Legitimate empty model results are not fallback.
    RecommendationEngine engine = RecommendationEngine::Temporal;
    qint64 generatedAtUtc = 0;
    bool containsSyntheticData = false;
};

Status validatePersonId(const QString& personId);
Status validate(const PersonUpdate& value);
Status validate(const PersonUpdatesRequest& value);
Status validate(const TrendingRequest& value);
Status validate(const FootprintRequest& value);
Status validate(const TemporalRecommendationRequest& value);
Status validate(const WorkFeedbackRequest& value);
Status validate(const PersonSearchRequest& value);

// Same authenticated online session as WarehouseService; no caller-supplied user ID.
// Callbacks run once on the owning thread, including errors. Immediate failure MAY be synchronous.
// Permission/DB failures are errors; an empty result means success with no data.
// Strings from external sources/models must be rendered as plain text.
class CareService {
public:
    virtual ~CareService() = default;
    // >>> DEFENSE-NOTE | 接口翻译｜recordBehavior
    // 记录一次主动行为；身份、时间和公共资格由服务端处理。
    // <<< DEFENSE-NOTE | 本段讲解结束
    virtual void recordBehavior(const BehaviorEvent& event, StatusCallback done) = 0;
    virtual void getParticipationSettings(Callback<ParticipationSettings> done) = 0;
    virtual void setParticipationSettings(const ParticipationSettings& settings, StatusCallback done) = 0;
    virtual void clearHistory(StatusCallback done) = 0;
    virtual void getResource(const WarehouseItemRef& source, Callback<ResourceSummary> done) = 0;
    virtual void searchPeople(const PersonSearchRequest& request, Callback<Page<PersonSummary>> done) = 0;
    virtual void searchPersonWeb(const QString&, Callback<QList<PersonWebSource>> done)
    {
        done({{warehouse_contract::ErrorCode::ServerError, QStringLiteral("当前服务未接通联网人物搜索。")}, {}});
    }
    virtual void listWorkFeedback(const PageRequest& page, Callback<Page<WorkFeedback>> done) = 0;
    virtual void setWorkFeedback(const WorkFeedbackRequest& request, StatusCallback done) = 0;
    virtual void listFollows(const PageRequest& page, Callback<Page<PersonSummary>> done) = 0;
    // >>> DEFENSE-NOTE | 接口翻译｜setFollow
    // 设置当前用户对指定稳定人物ID的关注状态，不能修改别人关注。
    // <<< DEFENSE-NOTE | 本段讲解结束
    virtual void setFollow(const QString& personId, bool followed, StatusCallback done) = 0;
    // >>> DEFENSE-NOTE | 接口翻译｜getPersonWorks
    // 仅返回当前用户有权读取的人物关联作品。
    // <<< DEFENSE-NOTE | 本段讲解结束
    virtual void getPersonWorks(const QString& personId, const PageRequest& page, Callback<Page<ResourceSummary>> done) = 0;
    // >>> DEFENSE-NOTE | 接口翻译｜getPersonUpdates
    // 读取指定人物的可信缓存资讯；不会接受任意抓取URL。
    // <<< DEFENSE-NOTE | 本段讲解结束
    virtual void getPersonUpdates(const PersonUpdatesRequest& request, Callback<Page<PersonUpdate>> done) = 0;
    // >>> DEFENSE-NOTE | 接口翻译｜getTrending
    // 请求已聚合热榜，Rules/Model决定是否额外调用模型文字层。
    // <<< DEFENSE-NOTE | 本段讲解结束
    virtual void getTrending(const TrendingRequest& request, Callback<TrendingResult> done) = 0;
    // >>> DEFENSE-NOTE | 接口翻译｜getFootprint
    // 请求本人足迹统计报告，数据错误与正常无历史区分。
    // <<< DEFENSE-NOTE | 本段讲解结束
    virtual void getFootprint(const FootprintRequest& request, Callback<FootprintReport> done) = 0;
    // Same handle ownership/cancellation rules as WarehouseService; distinct handles per pending request.
    // >>> DEFENSE-NOTE | 接口翻译｜recommendTemporal
    // 在线时序/场景推荐入口；异步返回有序结果，句柄用于取消。
    // <<< DEFENSE-NOTE | 本段讲解结束
    virtual warehouse_contract::RecommendationHandle recommendTemporal(
        const TemporalRecommendationRequest& request, Callback<TemporalRecommendationResult> done) = 0;
    // >>> DEFENSE-NOTE | 接口翻译｜cancelRecommendation
    // 取消指定在途推荐，防退出或切号后旧结果串入新页面。
    // <<< DEFENSE-NOTE | 本段讲解结束
    virtual bool cancelRecommendation(const warehouse_contract::RecommendationHandle& handle) = 0;
};
} // namespace care_contract
