#include "mediarecommendation.h"

#include <algorithm>

namespace {

struct ScoredRecord {
    QString id;
    int wholeScore;
    quint64 heatNumerator;
};

}  // namespace

QStringList recommendMedia(const QList<MediaRecord>& records,
                           const RecommendationOptions& options) {
    if (options.limit <= 0 || records.isEmpty()) {
        return {};
    }

    const QString category = options.preferredCategory.trimmed();
    const QString fileType = options.preferredFileType.trimmed();
    quint64 maxViews = 0;
    for (const MediaRecord& record : records) {
        maxViews = std::max(maxViews, record.viewCount);
    }

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
