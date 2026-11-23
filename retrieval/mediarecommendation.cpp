// >>> DEFENSE-NOTE | 文件导读｜基础规则评分
// 阅读路线：整理偏好 → 全库最大浏览次数 → 类别/格式/热度分 → 稳定排序 → 截取ID
// 联调关系：单机基础推荐/旧偏好服务复用；当前在线动态推荐用 care_temporal，不要混为一个公式。
// 标有“答辩导读”的内容仅为讲解注释；原有条件、接口和算法不变。
// <<< DEFENSE-NOTE | 本段讲解结束
#include "mediarecommendation.h"

#include <algorithm>

namespace {

// >>> DEFENSE-NOTE | ScoredRecord = 用于精确比较的评分行
// wholeScore 保存整数部分；heatNumerator 保存热度小数部分的分子。
// 类别匹配加2，格式匹配加1，热度=viewCount/maxViews；为避免大整数转浮点损失，拆开比较。
// <<< DEFENSE-NOTE | 本段讲解结束
struct ScoredRecord {
    QString id;
    int wholeScore;
    quint64 heatNumerator;
};

}  // namespace

// >>> DEFENSE-NOTE | recommendMedia = 按规则推荐媒体
// 输入完整记录和偏好，返回有序ID；limit<=0 或空库返回空。推荐是加分排序，不是硬过滤。
// <<< DEFENSE-NOTE | 本段讲解结束
QStringList recommendMedia(const QList<MediaRecord>& records,
                           const RecommendationOptions& options) {
    if (options.limit <= 0 || records.isEmpty()) {
        return {};
    }

    const QString category = options.preferredCategory.trimmed();
    const QString fileType = options.preferredFileType.trimmed();
    // >>> DEFENSE-NOTE | 步骤 01｜找热度共同分母
    // 先扫描全部记录求最大浏览次数；最大值为零时全体热度为零，不做除零。
    // <<< DEFENSE-NOTE | 本段讲解结束
    quint64 maxViews = 0;
    for (const MediaRecord& record : records) {
        maxViews = std::max(maxViews, record.viewCount);
    }

    // >>> DEFENSE-NOTE | 步骤 02｜每条作品算类别、格式和热度
    // 类别去首尾空格后精确比较；格式忽略大小写。热度恰好为1则记入整数部分，其余保留分子。
    // <<< DEFENSE-NOTE | 本段讲解结束
    QList<ScoredRecord> ranked;
    ranked.reserve(records.size());
    for (const MediaRecord& record : records) {
        const bool categoryMatches = !category.isEmpty() && record.category.trimmed() == category;
        const bool fileTypeMatches = !fileType.isEmpty()
            && record.fileType.trimmed().compare(fileType, Qt::CaseInsensitive) == 0;
        const bool heatIsOne = maxViews != 0 && record.viewCount == maxViews;
        const int wholeScore = (categoryMatches ? 2 : 0) + (fileTypeMatches ? 1 : 0)
            + (heatIsOne ? 1 : 0);

        // 热度恰好为 1 时计入整数分。其余热度共用同一个分母，直接比分子即可。
        ranked.append(ScoredRecord{record.id, wholeScore, heatIsOne ? 0 : record.viewCount});
    }

    // >>> DEFENSE-NOTE | 步骤 03｜排序后输出前 limit 个ID
    // 先比整数分，再比热度分子，最后按ID升序。整体有一次排序，一般 O(n log n)。
    // <<< DEFENSE-NOTE | 本段讲解结束
    std::sort(ranked.begin(), ranked.end(), [](const ScoredRecord& left, const ScoredRecord& right) {
        if (left.wholeScore != right.wholeScore) {
            return left.wholeScore > right.wholeScore;
        }
        if (left.heatNumerator != right.heatNumerator) {
            return left.heatNumerator > right.heatNumerator;
        }
        return left.id < right.id;
    });

    const qsizetype count = std::min<qsizetype>(options.limit, ranked.size());
    QStringList ids;
    ids.reserve(count);
    for (qsizetype index = 0; index < count; ++index) {
        ids.append(ranked[index].id);
    }
    return ids;
}
