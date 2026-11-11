#include "mediarecommendation.h"

#include <algorithm>

QStringList recommendMedia(const QList<MediaRecord>& records, const RecommendationOptions& options)
{
    if (options.limit <= 0 || records.isEmpty())
        return {};

    const QString category = options.preferredCategory.trimmed();
    const QString fileType = options.preferredFileType.trimmed();

    struct Candidate {
        const MediaRecord* record; // Record in the caller's input list.
        int matches;               // Number of selected preferences this record meets.
    };
    QList<Candidate> candidates;
    candidates.reserve(records.size());
    for (const auto& record : records) {
        const bool categoryMatch = !category.isEmpty() && record.category == category;
        const bool typeMatch = !fileType.isEmpty() &&
            record.fileType.trimmed().compare(fileType, Qt::CaseInsensitive) == 0;
        candidates.append({&record, static_cast<int>(categoryMatch) + static_cast<int>(typeMatch)});
    }

    // Compare view counts directly to preserve the entire unsigned 64-bit range.
    const auto better = [](const Candidate& a, const Candidate& b) {
        if (a.matches != b.matches)
            return a.matches > b.matches;
        if (a.record->viewCount != b.record->viewCount)
            return a.record->viewCount > b.record->viewCount;
        return a.record->id < b.record->id;
    };
    const auto count = qMin<qsizetype>(options.limit, candidates.size());
    std::partial_sort(candidates.begin(), candidates.begin() + count, candidates.end(), better);

    QStringList ids;
    ids.reserve(count);
    for (qsizetype i = 0; i < count; ++i)
        ids.append(candidates.at(i).record->id);
    return ids;
}
