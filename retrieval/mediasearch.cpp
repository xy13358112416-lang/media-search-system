#include "mediasearch.h"

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace {

// 将人名首尾空白去掉、类型转成大写，并清理重复演员。
MediaRecord normalizedRecord(MediaRecord record) {
    record.director = record.director.trimmed();
    record.fileType = record.fileType.trimmed().toUpper();
    for (QString& actor : record.actors) {
        actor = actor.trimmed();
    }
    record.actors.removeAll(QString{});
    record.actors.removeDuplicates();
    return record;
}

// 查询和索引使用同样的字段规范。ID 保持数据模块分配的原值。
SearchQuery normalizedQuery(const SearchQuery& query) {
    return {query.director.trimmed(), query.actor.trimmed(), query.fileType.trimmed().toUpper()};
}

using PostingIndex = QHash<QString, QStringList>;

// 移除某个字段值下的一条 ID。空列表的键一并清理。
void removePosting(PostingIndex& index, const QString& key, const QString& id) {
    auto bucket = index.find(key);  // key 对应的倒排列表。
    if (bucket == index.end()) {
        return;
    }
    auto position = std::lower_bound(bucket->begin(), bucket->end(), id);  // ID 的有序位置。
    if (position != bucket->end() && *position == id) {
        bucket->erase(position);
    }
    if (bucket->isEmpty()) {
        index.erase(bucket);
    }
}

struct ActivePosting {
    SearchField field;       // 当前筛选字段。
    const QStringList* ids;  // 指向索引中的有序列表，查询期间仅读取。
};

}  // namespace

void MediaSearch::rebuild(const QList<MediaRecord>& records) {
    MediaSearch loaded;  // 完整构建成功后才替换现有记录和索引。
    loaded.records_.reserve(records.size());
    for (const MediaRecord& record : records) {
        if (record.id.trimmed().isEmpty() || loaded.records_.contains(record.id)) {
            throw std::invalid_argument("Media record IDs must be nonempty and unique");
        }
        const MediaRecord entry = normalizedRecord(record);  // 用同一份规范化数据建索引。
        loaded.records_.insert(entry.id, entry);
        loaded.indexRecord(entry, true);
    }
    for (PostingIndex* index : {&loaded.directors_, &loaded.actors_, &loaded.fileTypes_}) {
        for (QStringList& ids : *index) {
            ids.sort(Qt::CaseSensitive);
        }
    }
    *this = std::move(loaded);
}

void MediaSearch::indexRecord(const MediaRecord& record, bool appendOnly) {
    // 仅为有值的字段建索引。新增时用二分查找维持 ID 顺序。
    const auto add = [&record, appendOnly](PostingIndex& index, const QString& key) {
        if (key.isEmpty()) {
            return;
        }
        QStringList& ids = index[key];  // key 对应的倒排列表。
        if (appendOnly) {
            ids.append(record.id);
        } else {
            ids.insert(std::lower_bound(ids.cbegin(), ids.cend(), record.id), record.id);
        }
    };
    add(directors_, record.director);
    for (const QString& actor : record.actors) {
        add(actors_, actor);
    }
    add(fileTypes_, record.fileType);
}

void MediaSearch::addRecord(const MediaRecord& record) {
    if (record.id.trimmed().isEmpty() || records_.contains(record.id)) {
        throw std::invalid_argument("Media record IDs must be nonempty and unique");
    }
    const MediaRecord entry = normalizedRecord(record);  // 增量更新沿用重建时的规范。
    records_.insert(entry.id, entry);
    indexRecord(entry, false);
}

void MediaSearch::removeRecord(const QString& id) {
    const auto entry = records_.constFind(id);  // 删除前取得原先参与索引的字段。
    if (entry == records_.cend()) {
        throw std::out_of_range("Media record ID not found in search index");
    }
    removePosting(directors_, entry->director, id);
    for (const QString& actor : entry->actors) {
        removePosting(actors_, actor, id);
    }
    removePosting(fileTypes_, entry->fileType, id);
    records_.erase(entry);
}

QStringList MediaSearch::search(const SearchQuery& query) const {
    return search(query, SearchStrategy::SmallestListFirst);
}

QStringList MediaSearch::search(const SearchQuery& query, SearchStrategy strategy,
                                SearchTrace* trace) const {
    if (trace) {
        *trace = {};
    }
    const SearchQuery filters = normalizedQuery(query);  // 两条执行路径共用过滤规则。
    if (strategy == SearchStrategy::LinearScan) {
        QStringList matches;  // 逐条扫描得到的 ID。
        if (trace) {
            trace->recordsScanned = records_.size();
        }
        for (const MediaRecord& record : records_) {
            if ((!filters.director.isEmpty() && record.director != filters.director)
                || (!filters.actor.isEmpty() && !record.actors.contains(filters.actor))
                || (!filters.fileType.isEmpty() && record.fileType != filters.fileType)) {
                continue;
            }
            matches.append(record.id);
        }
        matches.sort(Qt::CaseSensitive);
        return matches;
    }

    QList<ActivePosting> active;  // 只处理用户实际填写的查询条件。
    active.reserve(3);
    static const QStringList empty;  // 不存在的键命中零条记录。
    const auto collect = [&active](const PostingIndex& index, const QString& key, SearchField field) {
        if (!key.isEmpty()) {
            const auto found = index.constFind(key);
            active.append({field, found == index.cend() ? &empty : &found.value()});
        }
    };
    collect(directors_, filters.director, SearchField::Director);
    collect(actors_, filters.actor, SearchField::Actor);
    collect(fileTypes_, filters.fileType, SearchField::FileType);

    if (active.isEmpty()) {
        QStringList ids = records_.keys();  // 空查询返回全部 ID。
        ids.sort(Qt::CaseSensitive);
        return ids;
    }

    if (strategy == SearchStrategy::SmallestListFirst) {
        // 优先从候选少的条件开始，同样长时保留导演、演员、类型的固定顺序。
        std::sort(active.begin(), active.end(), [](const ActivePosting& left, const ActivePosting& right) {
            return left.ids->size() == right.ids->size()
                ? left.field < right.field : left.ids->size() < right.ids->size();
        });
    }

    QStringList matches = *active.first().ids;  // 第一项提供初始候选。
    if (trace) {
        trace->steps.append({active.first().field, matches.size(), matches.size()});
    }
    for (qsizetype i = 1; i < active.size() && !matches.isEmpty(); ++i) {
        const ActivePosting& posting = active[i];  // 与现有候选取有序交集。
        QStringList intersection;
        std::set_intersection(matches.cbegin(), matches.cend(), posting.ids->cbegin(),
                              posting.ids->cend(), std::back_inserter(intersection));
        matches.swap(intersection);
        if (trace) {
            trace->steps.append({posting.field, posting.ids->size(), matches.size()});
        }
    }
    return matches;
}
