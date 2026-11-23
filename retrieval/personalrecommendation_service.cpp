// >>> DEFENSE-NOTE | 文件导读｜单机基于历史补全偏好
// 阅读路线：核对会话 → 当前媒体快照 → 历史聚合 → Auto/Any/Explicit 解析 → 基础规则推荐
// 联调关系：通过 AccountHistoryService 读当前用户历史，通过 MediaProvider 获取媒体；不直接执行SQL。在线时序评分有独立实现。
// 标有“答辩导读”的内容仅为讲解注释；原有条件、接口和算法不变。
// <<< DEFENSE-NOTE | 本段讲解结束
#include "personalrecommendation_service.h"
#include "mediarecommendation.h"
#include <QHash>
#include <algorithm>
#include <exception>
#include <limits>
#include <utility>

namespace recommendation_contract {
namespace {
// >>> DEFENSE-NOTE | failure = 统一构造失败结果
// success=false，带错误信息，清空ID与理由。读取失败与成功空结果必须区分。
// <<< DEFENSE-NOTE | 本段讲解结束
RecommendationResult failure(const QString& error) { return {false, error, {}, {}}; }
// >>> DEFENSE-NOTE | valid = 验证偏好选择
// Auto自动推断、Any不使用此偏好、Explicit明确值；Explicit不能传空值，未知枚举拒绝。
// <<< DEFENSE-NOTE | 本段讲解结束
bool valid(const PreferenceSelection& choice) {
    switch (choice.mode) {
    case PreferenceMode::Auto:
    case PreferenceMode::Any: return true;
    case PreferenceMode::Explicit: return !choice.value.trimmed().isEmpty();
    }
    return false;
}
struct Interest { quint64 clicks = 0; qint64 lastViewedAt = 0; };
using Interests = QHash<QString, Interest>;
// >>> DEFENSE-NOTE | accumulate = 累加一个字段的历史兴趣
// QHash 按类别/格式累计浏览次数，保留最近浏览时间；相加前检查无符号整数溢出。
// <<< DEFENSE-NOTE | 本段讲解结束
bool accumulate(Interests& interests, const QString& value, const account_contract::MediaHistoryEntry& entry) {
    if (value.isEmpty()) return true;
    auto& interest = interests[value];
    const auto count = static_cast<quint64>(entry.viewCount);
    if (count > std::numeric_limits<quint64>::max() - interest.clicks) return false;
    interest.clicks += count;
    interest.lastViewedAt = std::max(interest.lastViewedAt, entry.lastViewedAt);
    return true;
}
// >>> DEFENSE-NOTE | favorite = 选出最偏好的字段值
// 次数最多者优先，次数相同选最近浏览，再相同按字符串升序；不依赖哈希遍历顺序。
// <<< DEFENSE-NOTE | 本段讲解结束
QString favorite(const Interests& interests) {
    QString best;
    Interest score;
    for (auto it = interests.cbegin(); it != interests.cend(); ++it) {
        const auto& candidate = it.value();
        if (best.isEmpty() || candidate.clicks > score.clicks ||
            (candidate.clicks == score.clicks && (candidate.lastViewedAt > score.lastViewedAt ||
             (candidate.lastViewedAt == score.lastViewedAt && it.key() < best)))) {
            best = it.key(); score = candidate;
        }
    }
    return best;
}
// >>> DEFENSE-NOTE | explanation = 生成偏好来源说明
// 区分本次明确选择、不限、从历史补全、没有可用历史；不是大模型生成的文本。
// <<< DEFENSE-NOTE | 本段讲解结束
QString explanation(const QString& field, const PreferenceSelection& choice, const QString& resolved) {
    if (choice.mode == PreferenceMode::Any) return field + QStringLiteral("：不限");
    if (choice.mode == PreferenceMode::Explicit) return field + QStringLiteral("：按本次选择“") + resolved + QStringLiteral("”");
    if (resolved.isEmpty()) return field + QStringLiteral("：暂无可用历史偏好");
    return field + QStringLiteral("：根据历史浏览偏好选择“") + resolved + QStringLiteral("”");
}
}

PersonalRecommendationServiceImpl::PersonalRecommendationServiceImpl(
    account_contract::AccountHistoryService& accounts, MediaProvider currentMedia)
    : accounts_(accounts), currentMedia_(std::move(currentMedia)) {}

// >>> DEFENSE-NOTE | recommend = 单机个性化推荐总入口
// 只允许当前登录用户请求自己的推荐，不能仅相信 request.userId。
// 依次验证请求、取得媒体快照、建立ID查找表、按需要读取历史；异常明确返回失败。
// <<< DEFENSE-NOTE | 本段讲解结束
RecommendationResult PersonalRecommendationServiceImpl::recommend(const RecommendationRequest& request) {
    const auto user = accounts_.currentUser();
    if (!user || request.userId <= 0 || user->userId != request.userId)
        return failure(QStringLiteral("请使用当前登录用户请求推荐。"));
    if (!valid(request.category) || !valid(request.fileType))
        return failure(QStringLiteral("推荐偏好模式无效，或指定值为空。"));
    if (request.limit <= 0) return {true, {}, {}, QStringLiteral("推荐数量为零。")};
    if (!currentMedia_) return failure(QStringLiteral("当前媒体数据服务不可用。"));

    QList<MediaRecord> snapshot;
    try { snapshot = currentMedia_(); }
    catch (const std::exception&) { return failure(QStringLiteral("读取当前媒体数据失败。")); }
    const QList<MediaRecord> records = std::move(snapshot);
    if (records.isEmpty()) return {true, {}, {}, QStringLiteral("媒体库暂无可推荐记录。")};
    // >>> DEFENSE-NOTE | 步骤 01｜建立当前媒体查找表
    // ID→记录指针，便于将历史 mediaId 关联到当前作品。指针仅在本次 records 生命周期内使用。
    // 空ID/重复ID说明输入不可靠，终止推荐。
    // <<< DEFENSE-NOTE | 本段讲解结束
    QHash<QString, const MediaRecord*> byId;
    byId.reserve(records.size());
    for (const auto& record : records) {
        if (record.id.isEmpty() || byId.contains(record.id))
            return failure(QStringLiteral("媒体数据包含无效或重复的编号。"));
        byId.insert(record.id, &record);
    }

    // >>> DEFENSE-NOTE | 步骤 02｜仅 Auto 字段读取历史
    // 两项都是Any或Explicit时不必查历史。已删除媒体的历史跳过；负次数是错误，零次数不贡献。
    // <<< DEFENSE-NOTE | 本段讲解结束
    Interests categories, types;
    if (request.category.mode == PreferenceMode::Auto || request.fileType.mode == PreferenceMode::Auto) {
        const auto history = accounts_.loadMediaHistory(request.userId);
        if (!history.status.ok() || !history.value)
            return failure(history.status.message.isEmpty() ? QStringLiteral("读取用户浏览历史失败。") : history.status.message);
        for (const auto& entry : *history.value) {
            const auto found = byId.constFind(entry.mediaId);
            if (found == byId.cend()) continue; // Deleted media no longer contributes to interests.
            if (entry.viewCount < 0) return failure(QStringLiteral("浏览历史包含无效次数。"));
            if (entry.viewCount == 0) continue;
            const auto& record = **found;
            if ((request.category.mode == PreferenceMode::Auto &&
                 !accumulate(categories, record.category.trimmed(), entry)) ||
                (request.fileType.mode == PreferenceMode::Auto &&
                 !accumulate(types, record.fileType.trimmed().toUpper(), entry)))
                return failure(QStringLiteral("累计浏览次数超出支持范围。"));
        }
    }

    // >>> DEFENSE-NOTE | 步骤 03｜将三态偏好转成实际字段值
    // Explicit采用用户值；Auto使用 favorite；Any返回空值，明确禁止历史补全。
    // 之后调用 recommendMedia 评分，返回ID及整体理由，Qt按既定顺序展示。
    // <<< DEFENSE-NOTE | 本段讲解结束
    const auto resolve = [](const PreferenceSelection& choice, const Interests& interests) {
        if (choice.mode == PreferenceMode::Explicit) return choice.value.trimmed();
        return choice.mode == PreferenceMode::Auto ? favorite(interests) : QString();
    };
    RecommendationOptions options{resolve(request.category, categories),
                                  resolve(request.fileType, types).toUpper(), request.limit};
    const QString reason = explanation(QStringLiteral("类别"), request.category, options.preferredCategory)
        + QStringLiteral("；") + explanation(QStringLiteral("文件类型"), request.fileType, options.preferredFileType)
        + QStringLiteral("；结合媒体库热度排序。");
    return {true, {}, recommendMedia(records, options), reason};
}
} // namespace recommendation_contract
