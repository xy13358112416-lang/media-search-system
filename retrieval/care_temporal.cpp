// >>> DEFENSE-NOTE | 文件导读｜时序推荐的纯计算核心
// 阅读路线：校验 → 历史衰减画像 → 候选评分 → 稳定排序 → 类别多样性 → 有序结果
// 联调关系：服务器构造已授权 PersonalSnapshot；本文件不读 SQLite、不调用模型；Qt 展示返回的作品与数值理由。
// 标有“答辩导读”的内容仅为讲解注释；原有条件、接口和算法不变。
// <<< DEFENSE-NOTE | 本段讲解结束
#include "care_temporal.h"
#include "care_algorithm_common.h"
#include <QDateTime>
#include <QMap>
#include <QTimeZone>
#include <algorithm>
#include <cmath>

namespace care_algorithms {
namespace {
using namespace care_contract;
using Mode = recommendation_contract::PreferenceMode;
// >>> DEFENSE-NOTE | 数据结构｜Profile = 兴趣画像
// categories 用 QMap 保存“类别→衰减贡献”；people 保存“人物ID→衰减贡献”。
// 两个 Total 是归一化分母，不是不同作品数。synthetic 标志记录画像是否用到了模拟历史。
// <<< DEFENSE-NOTE | 本段讲解结束
struct Profile {
    QMap<QString, double> categories;
    QMap<QString, double> people;
    double categoryTotal = 0;
    double peopleTotal = 0;
    bool syntheticCategories = false;
    bool syntheticPeople = false;
};
struct Interest {
    double score = 0;
    bool synthetic = false;
};
// >>> DEFENSE-NOTE | 数据结构｜ScoredWork = 一个候选的评分明细
// 每个候选保存原始作品、长期/近期兴趣、热度、新鲜度、显式偏好和关注加分。
// total 用于排序；分项用于 reason() 输出可核对的推荐解释。
// <<< DEFENSE-NOTE | 本段讲解结束
struct ScoredWork {
    ResourceSummary work;
    QString category;
    double longScore = 0;
    double recentScore = 0;
    double heat = 0;
    double freshness = 0;
    double categoryBonus = 0;
    double typeBonus = 0;
    double followBonus = 0;
    double total = 0;
    bool synthetic = false;
};
// >>> DEFENSE-NOTE | 辅助函数｜finiteNonnegative = 有限且非负
// 排除 NaN、无穷大和负数，防止无效权重污染比较与排序。
// <<< DEFENSE-NOTE | 本段讲解结束
bool finiteNonnegative(double value) { return std::isfinite(value) && value >= 0; }
// >>> DEFENSE-NOTE | 辅助函数｜validateConfig = 验证评分配置
// 半衰期必须大于零，权重和奖励在合法范围；类别与人物的内部权重和不能超过1。
// 参数错误直接返回 InvalidArgument，不偷偷换一套参数继续算。
// <<< DEFENSE-NOTE | 本段讲解结束
Status validateConfig(const TemporalScoringConfig& config) {
    for (const auto value : {config.longHalfLifeDays, config.recentHalfLifeDays, config.freshnessHalfLifeDays})
        if (!std::isfinite(value) || value <= 0)
            return {warehouse_contract::ErrorCode::InvalidArgument, QStringLiteral("评分半衰期必须为有限正数。")};
    for (const auto value : {config.categoryWeight, config.peopleWeight, config.longWeight, config.recentWeight,
                            config.heatWeight, config.freshnessWeight, config.explicitCategoryBonus,
                            config.explicitTypeBonus, config.followBonus})
        if (!finiteNonnegative(value) || value > 1)
            return {warehouse_contract::ErrorCode::InvalidArgument, QStringLiteral("评分权重必须为0到1的有限数值。")};
    if (config.categoryWeight + config.peopleWeight > 1)
        return {warehouse_contract::ErrorCode::InvalidArgument, QStringLiteral("兴趣匹配权重之和不能超过1。")};
    return {};
}
// >>> DEFENSE-NOTE | 辅助函数｜personIds = 收集唯一人物身份
// QSet 按 personId 去重：同一人多个角色，在单次浏览的兴趣累加里只计一次。
// 姓名不是身份主键，不能因为同名就合并人物。
// <<< DEFENSE-NOTE | 本段讲解结束
QSet<QString> personIds(const ResourceSummary& work) {
    QSet<QString> result;
    for (const auto& person : work.people) result.insert(person.personId);
    return result;
}
// >>> DEFENSE-NOTE | 辅助函数｜addView = 将一次浏览累加到画像
// 输入是已带作品快照的浏览、衰减权重和是否启用类别画像。
// 以引用修改传入 Profile；同一事件会分别以长期权重、近期权重调用此函数。
// <<< DEFENSE-NOTE | 本段讲解结束
void addView(Profile& profile, const ObservedBehavior& event, double weight, bool useCategory) {
    if (weight <= 0) return;
    const auto& work = *event.workSnapshot;
    const auto category = detail::normalizeText(work.media.category);
    // >>> DEFENSE-NOTE | addView 步骤1｜累计类别贡献
    // Auto 模式才启用历史类别补全；Any/Explicit 不从历史推断类别。
    // 一次浏览的 weight 加到对应类别，同时加到类别总贡献；未知空类别不硬造标签。
    // <<< DEFENSE-NOTE | 本段讲解结束
    if (useCategory && !category.isEmpty()) {
        profile.categories[category] += weight;
        profile.categoryTotal += weight;
        profile.syntheticCategories |= event.record.synthetic;
    }
    // QSet eliminates duplicate credits; sorting also keeps floating-point sums
    // independent of the hash seed/process when a view credits several people.
    // >>> DEFENSE-NOTE | addView 步骤2｜累计人物贡献
    // 先按ID去重，再排序后累加，保证浮点加法顺序稳定。
    // 人物兴趣与类别开关独立；单部作品有多个人物时，peopleTotal 会增加多次。
    // <<< DEFENSE-NOTE | 本段讲解结束
    auto people = personIds(work).values();
    std::sort(people.begin(), people.end());
    for (const auto& id : people) {
        profile.people[id] += weight;
        profile.peopleTotal += weight;
        profile.syntheticPeople |= event.record.synthetic;
    }
}
// >>> DEFENSE-NOTE | 辅助函数｜match = 计算作品与一份画像的匹配度
// 输入可以是长期画像或近期画像，算法相同，只是历史权重不同。
// 类别贡献除以 categoryTotal；人物贡献除以 peopleTotal，最后按内部权重组合。
// <<< DEFENSE-NOTE | 本段讲解结束
Interest match(const Profile& profile, const ResourceSummary& work, const TemporalScoringConfig& config) {
    Interest result;
    const auto category = detail::normalizeText(work.media.category);
    const auto people = personIds(work);
    const bool hasCategory = profile.categoryTotal > 0;
    const bool hasPeople = profile.peopleTotal > 0 && !people.isEmpty();
    double categoryWeight = config.categoryWeight;
    // >>> DEFENSE-NOTE | match 细节｜没有可匹配人物时重新分配内部权重
    // 缺少人物特征时，把人物那部分匹配权重转给类别，避免元数据缺失使作品天然吃亏。
    // 这不等于虚构人物偏好；类别也无历史时，类别匹配仍为零。
    // <<< DEFENSE-NOTE | 本段讲解结束
    if (!hasPeople) categoryWeight += config.peopleWeight;
    const double categoryMatch = hasCategory ? profile.categories.value(category) / profile.categoryTotal : 0;
    double peopleMatch = 0;
    if (hasPeople) {
        auto ordered = people.values(); std::sort(ordered.begin(), ordered.end());
        for (const auto& id : ordered) peopleMatch += profile.people.value(id) / profile.peopleTotal;
    }
    result.score = categoryWeight * categoryMatch + config.peopleWeight * peopleMatch;
    result.synthetic = (categoryWeight > 0 && categoryMatch > 0 && profile.syntheticCategories) ||
                       (config.peopleWeight > 0 && peopleMatch > 0 && profile.syntheticPeople);
    return result;
}
// >>> DEFENSE-NOTE | 辅助函数｜hasInterest = 是否存在有效个人兴趣
// 同时看画像是否有贡献、对应配置权重是否启用。
// 无个人特征时进入热度/新鲜度冷启动分支；数据库失败不是冷启动。
// <<< DEFENSE-NOTE | 本段讲解结束
bool hasInterest(const Profile& profile, const TemporalScoringConfig& config) {
    return ((config.categoryWeight + config.peopleWeight > 0) && profile.categoryTotal > 0) ||
           (config.peopleWeight > 0 && profile.peopleTotal > 0);
}
// >>> DEFENSE-NOTE | 辅助函数｜reason = 格式化数值推荐理由
// 将实际分项、实际权重、加分和总分拼成文本。
// 这是规则算法的解释，不是 DeepSeek 生成的理由。
// <<< DEFENSE-NOTE | 本段讲解结束
QString reason(const ScoredWork& work, double longWeight, double recentWeight,
               double heatWeight, double freshWeight) {
    return QStringLiteral("长期兴趣=%1（权重%2）；近期兴趣=%3（权重%4）；公共热度=%5（权重%6）；"
                          "新鲜度=%7（权重%8）；显式类别=%9；显式类型=%10；主动关注=%11；总分=%12。")
        .arg(work.longScore, 0, 'f', 6).arg(longWeight, 0, 'f', 6)
        .arg(work.recentScore, 0, 'f', 6).arg(recentWeight, 0, 'f', 6)
        .arg(work.heat, 0, 'f', 6).arg(heatWeight, 0, 'f', 6)
        .arg(work.freshness, 0, 'f', 6).arg(freshWeight, 0, 'f', 6)
        .arg(work.categoryBonus, 0, 'f', 6).arg(work.typeBonus, 0, 'f', 6)
        .arg(work.followBonus, 0, 'f', 6).arg(work.total, 0, 'f', 6);
}
} // namespace

care_contract::Result<care_contract::TemporalRecommendationResult> recommendTemporal(
    const care_contract::TemporalRecommendationRequest& request, const PersonalSnapshot& snapshot) {
    // >>> DEFENSE-NOTE | 入口重载｜使用默认配置
    // 两参数版本委托三参数版本；TemporalScoringConfig{} 装入默认半衰期与权重。
    // 讲解默认参数时打开 include/care_temporal.h，不需要背构造语法。
    // <<< DEFENSE-NOTE | 本段讲解结束
    return recommendTemporal(request, snapshot, TemporalScoringConfig{});
}
care_contract::Result<care_contract::TemporalRecommendationResult> recommendTemporal(
    const care_contract::TemporalRecommendationRequest& request, const PersonalSnapshot& snapshot,
    const TemporalScoringConfig& config) {
    // >>> DEFENSE-NOTE | 总流程 01｜先校验请求与评分参数
    // 检查偏好模式、数量、场景枚举，再检查配置数值。
    // 返回结构包含 status 与可选结果；失败立即结束，Qt 不应将失败当作空推荐。
    // <<< DEFENSE-NOTE | 本段讲解结束
    auto status = care_contract::validate(request);
    if (!status.ok()) return {status, {}};
    status = validateConfig(config);
    if (!status.ok()) return {status, {}};
    // Validate data even when personalization is switched off. Database failures
    // and corrupt history must never silently become a cold-start success.
    // >>> DEFENSE-NOTE | 总流程 02｜清洗可信输入
    // personalEvents 验证时间、身份和事件ID冲突，并去除不属于当前用户/未来的行为。
    // canonicalWorks 对可读作品去重并校验来源；负热度属于异常数据。即使关闭个性化也先验证数据。
    // <<< DEFENSE-NOTE | 本段讲解结束
    const auto events = detail::personalEvents(snapshot);
    if (!events.status.ok()) return {events.status, {}};
    const auto works = detail::canonicalWorks(snapshot.readableWorks);
    if (!works.status.ok()) return {works.status, {}};
    for (auto it = snapshot.publicHeat.cbegin(); it != snapshot.publicHeat.cend(); ++it)
        if (it.value() < 0) return {detail::serverError(QStringLiteral("公共热度计数无效。")), {}};

    // >>> DEFENSE-NOTE | 总流程 03｜创建长短期画像和作品查找表
    // longProfile/recentProfile 是两套类别/人物分布；currentWorks 是 workId→当前作品的 QHash。
    // 优先用历史快照；旧事件没有快照时才尝试当前可读作品，找不到就跳过。
    // <<< DEFENSE-NOTE | 本段讲解结束
    Profile longProfile, recentProfile;
    QHash<QString, ResourceSummary> currentWorks;
    for (const auto& work : *works.value) currentWorks.insert(work.resource.workId, work);
    if (snapshot.settings.personalizationEnabled) {
        for (const auto& event : *events.value) {
            if (event.record.event.action != BehaviorAction::View) continue;
            auto enriched = event;
            if (!enriched.workSnapshot) {
                const auto found = currentWorks.constFind(event.record.event.resource->workId);
                if (found == currentWorks.cend()) continue;
                enriched.workSnapshot = found.value();
            }
            // >>> DEFENSE-NOTE | 总流程 04｜同一次浏览，以两种速度衰减
            // age 是秒差除86400得到的天数；exp2(-age/半衰期) 即 2 的该次幂。
            // 默认长期半衰期90天、近期7天；7天前的浏览在近期画像权重0.5，长期仍约0.948。
            // 这里按浏览事件构建兴趣，不把搜索词当成已浏览作品；个性化关闭时不建立个人画像。
            // <<< DEFENSE-NOTE | 本段讲解结束
            const double age = static_cast<double>(snapshot.nowUtc - event.record.occurredAtUtc) / 86400.0;
            addView(longProfile, enriched, std::exp2(-age / config.longHalfLifeDays), request.category.mode == Mode::Auto);
            addView(recentProfile, enriched, std::exp2(-age / config.recentHalfLifeDays), request.category.mode == Mode::Auto);
        }
    }

    // >>> DEFENSE-NOTE | 总流程 05｜用北京时间确定发行日期新鲜度的参照日
    // 历史衰减按UTC秒差；发行日期是自然日，使用 Asia/Shanghai 得到今天。
    // 不依赖客户端电脑选择的时区；无效时区明确返回错误。
    // <<< DEFENSE-NOTE | 本段讲解结束
    const QTimeZone zone("Asia/Shanghai");
    if (!zone.isValid()) return {detail::serverError(QStringLiteral("北京时间时区数据不可用。")), {}};
    const auto today = QDateTime::fromSecsSinceEpoch(snapshot.nowUtc, zone).date();
    if (!today.isValid()) return {detail::serverError(QStringLiteral("北京时间日期不可用。")), {}};
    // >>> DEFENSE-NOTE | 总流程 06｜遍历候选并准备分项
    // 候选来自已授权可读作品；不感兴趣的 workId 直接排除。
    // QList 保存候选评分行，maxHeat 为后续热度归一化找分母。
    // 仅已知且不晚于今天的发行日期参与新鲜度；未知日期不补造。
    // <<< DEFENSE-NOTE | 本段讲解结束
    QList<ScoredWork> scored;
    qint64 maxHeat = 0;
    bool anyFreshness = false;
    for (const auto& work : *works.value) {
        if (snapshot.notInterestedWorkIds.contains(work.resource.workId)) continue;
        ScoredWork row; row.work = work; row.category = detail::normalizeText(work.media.category);
        maxHeat = std::max(maxHeat, snapshot.publicHeat.value(work.resource.workId));
        const auto date = work.media.releaseDate;
        if (date.isValid() && date <= today)
            row.freshness = std::exp2(-static_cast<double>(date.daysTo(today)) / config.freshnessHalfLifeDays);
        anyFreshness |= row.freshness > 0;
        scored.append(row);
    }
    // >>> DEFENSE-NOTE | 总流程 07｜判断是否需要冷启动
    // 有有效个人画像时使用长期/近期权重；没有时将这两项权重设为零。
    // 从可用的热度与新鲜度中重新归一化权重。没有历史是正常结果，读取历史失败前面已报错。
    // <<< DEFENSE-NOTE | 本段讲解结束
    const bool personal = (config.longWeight > 0 && hasInterest(longProfile, config)) ||
                          (config.recentWeight > 0 && hasInterest(recentProfile, config));
    double longWeight = personal ? config.longWeight : 0;
    double recentWeight = personal ? config.recentWeight : 0;
    double heatWeight = config.heatWeight;
    double freshWeight = config.freshnessWeight;
    if (!personal) {
        heatWeight = maxHeat > 0 ? heatWeight : 0;
        freshWeight = anyFreshness ? freshWeight : 0;
        const double available = heatWeight + freshWeight;
        if (available > 0) { heatWeight /= available; freshWeight /= available; }
    }
    // >>> DEFENSE-NOTE | 总流程 08｜准备热度对数归一化和显式偏好
    // log1p(x) 计算 ln(1+x)，压缩极端热门的数量差距。
    // 某候选热度 / 最大热度不是这里的公式；这里是 ln(1+该热度)/ln(1+最大热度)。
    // <<< DEFENSE-NOTE | 本段讲解结束
    const double heatDenominator = std::log1p(static_cast<double>(maxHeat));
    const auto categoryPreference = detail::normalizeText(request.category.value);
    const auto typePreference = detail::normalizeText(request.fileType.value);
    bool anyPositive = false;
    // >>> DEFENSE-NOTE | 总流程 09｜逐个作品计算总分
    // 同一作品分别匹配长期/近期画像，再加公共热度、新鲜度、显式类别/格式奖励、关注人物奖励。
    // 默认主权重：长期0.35、近期0.40、公共热度0.15、新鲜度0.10。
    // 显式偏好是软加分，不是检索式硬过滤；关注奖励只加一次，不按角色数叠加。
    // <<< DEFENSE-NOTE | 本段讲解结束
    for (auto& row : scored) {
        const auto longInterest = match(longProfile, row.work, config);
        const auto recentInterest = match(recentProfile, row.work, config);
        row.longScore = longInterest.score;
        row.recentScore = recentInterest.score;
        if (heatDenominator > 0)
            row.heat = std::log1p(static_cast<double>(snapshot.publicHeat.value(row.work.resource.workId))) / heatDenominator;
        if (request.category.mode == Mode::Explicit && row.category == categoryPreference)
            row.categoryBonus = config.explicitCategoryBonus;
        const auto type = detail::normalizeText(row.work.media.fileType);
        if (request.fileType.mode == Mode::Explicit && !type.isEmpty() && type == typePreference)
            row.typeBonus = config.explicitTypeBonus;
        if (snapshot.settings.personalizationEnabled) {
            for (const auto& person : row.work.people)
                if (snapshot.followedPeople.contains(person.personId)) { row.followBonus = config.followBonus; break; }
        }
        // >>> DEFENSE-NOTE | 评分公式｜把各分项组合
        // total = 长期权重×长期匹配 + 近期权重×近期匹配 + 热度权重×热度 + 新鲜度权重×新鲜度 + 三项奖励。
        // 分数用于相对排序，不是点击概率；加分存在时不能把它宣称为0到1的概率。
        // <<< DEFENSE-NOTE | 本段讲解结束
        row.total = longWeight * row.longScore + recentWeight * row.recentScore + heatWeight * row.heat +
                    freshWeight * row.freshness + row.categoryBonus + row.typeBonus + row.followBonus;
        row.synthetic = (longWeight > 0 && longInterest.synthetic) || (recentWeight > 0 && recentInterest.synthetic);
        row.synthetic |= heatWeight > 0 && row.heat > 0 && snapshot.publicHeatContainsSyntheticData;
        anyPositive |= row.total > 0;
    }
    // >>> DEFENSE-NOTE | 总流程 10｜有正分候选时，不用零分作品凑数
    // remove_if 找出应移除的行，再 erase 真正缩短容器。
    // 如果所有候选都是零分，则保留候选，后续使用稳定ID顺序提供确定性结果。
    // <<< DEFENSE-NOTE | 本段讲解结束
    if (anyPositive) scored.erase(std::remove_if(scored.begin(), scored.end(), [](const auto& row) {
        return row.total <= 0;
    }), scored.end());
    // >>> DEFENSE-NOTE | 总流程 11｜按分数排序，同分按稳定身份
    // 先按 total 降序，再按 workId、warehouseId、itemId 升序。
    // 消除哈希遍历顺序带来的漂移。排序一般 O(n log n)，整个推荐不是 O(1)。
    // <<< DEFENSE-NOTE | 本段讲解结束
    std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
        if (a.total != b.total) return a.total > b.total;
        if (a.work.resource.workId != b.work.resource.workId) return a.work.resource.workId < b.work.resource.workId;
        const auto& x = a.work.resource.source; const auto& y = b.work.resource.source;
        if (x.warehouseId != y.warehouseId) return x.warehouseId < y.warehouseId;
        return x.itemId < y.itemId;
    });

    // >>> DEFENSE-NOTE | 总流程 12｜挑选结果并控制类别连排
    // 连续两条同类别后，尝试提前选择另一个正分类别，避免第三条继续同类。
    // 没有其他合适类别时仍可继续同类；这是贪心重排，不承诺类别完全均衡。
    // 列表中间 takeAt 可能移动元素，不能把整段出队选择无条件说成 O(n)。
    // <<< DEFENSE-NOTE | 本段讲解结束
    TemporalRecommendationResult result;
    result.generatedAtUtc = snapshot.nowUtc;
    QString previousCategory;
    int run = 0;
    while (!scored.isEmpty() && result.items.size() < request.limit) {
        qsizetype next = 0;
        if (anyPositive && run >= 2 && scored.first().category == previousCategory) {
            for (qsizetype i = 1; i < scored.size(); ++i) {
                if (scored[i].total > 0 && scored[i].category != previousCategory) { next = i; break; }
            }
        }
        const auto row = scored.takeAt(next);
        // >>> DEFENSE-NOTE | 总流程 13｜保留作品、理由与模拟来源
        // 输出顺序就是展示顺序；Qt 不应再次按自己的偏好重排。
        // containsSyntheticData 标注结果是否依赖模拟历史/热度，不把演示数据说成真实用户效果。
        // <<< DEFENSE-NOTE | 本段讲解结束
        result.items.append({row.work, reason(row, longWeight, recentWeight, heatWeight, freshWeight)});
        result.containsSyntheticData |= row.synthetic;
        if (row.category == previousCategory) ++run;
        else { previousCategory = row.category; run = 1; }
    }
    if (result.items.isEmpty()) result.message = QStringLiteral("暂无符合条件的可读推荐作品。");
    return {{}, result};
}
} // namespace care_algorithms
