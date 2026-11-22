// >>> DEFENSE-NOTE | 文件导读｜导演、演员、格式的精确组合检索
// 阅读路线：规范化 → 建倒排表 → 查询列表 → 短列表优先 → 双指针交集
// 联调关系：主逻辑/Qt 提供已加载记录和稳定ID；本模块返回ID，不生成ID、不读数据库、不鉴权。
// 标有“答辩导读”的内容仅为讲解注释；原有条件、接口和算法不变。
// <<< DEFENSE-NOTE | 本段讲解结束
#include "mediasearch.h"

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace {

// 将人名首尾空白去掉、类型转成大写，并清理重复演员。
// >>> DEFENSE-NOTE | normalizedRecord = 规范化记录
// 复制记录后去掉人名首尾空白、格式转大写、清理空和重复演员；保持媒体ID原值。
// <<< DEFENSE-NOTE | 本段讲解结束
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
// >>> DEFENSE-NOTE | normalizedQuery = 规范化查询
// 与建索引采用一致规范，避免 mp4/MP4 或姓名首尾空格导致漏查。
// <<< DEFENSE-NOTE | 本段讲解结束
SearchQuery normalizedQuery(const SearchQuery& query) {
    return {query.director.trimmed(), query.actor.trimmed(), query.fileType.trimmed().toUpper()};
}

using PostingIndex = QHash<QString, QStringList>;

// 移除某个字段值下的一条 ID。空列表的键一并清理。
// >>> DEFENSE-NOTE | removePosting = 删除一条倒排关联
// QHash 定位字段列表；lower_bound 二分找ID位置；删除后空列表的键也清理。
// 找位置 O(log n)，但列表 erase 可能移动元素，总删除操作不一定是 O(log n)。
// <<< DEFENSE-NOTE | 本段讲解结束
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

// >>> DEFENSE-NOTE | ActivePosting = 当前生效的条件列表
// 保存字段枚举和指向已有有序列表的只读指针；避免复制每一个条件列表。
// <<< DEFENSE-NOTE | 本段讲解结束
struct ActivePosting {
    SearchField field;       // 当前筛选字段。
    const QStringList* ids;  // 指向索引中的有序列表，查询期间仅读取。
};

}  // namespace

// >>> DEFENSE-NOTE | rebuild = 完整重建索引
// 输入全部媒体记录；先构建 loaded 临时对象，空ID/重复ID抛异常时旧对象仍可用。
// 先 reserve 预留容量，批量追加倒排ID，最后统一排序并移动替换。不是数据库事务或多线程原子操作。
// <<< DEFENSE-NOTE | 本段讲解结束
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
    // >>> DEFENSE-NOTE | 重建步骤｜统一排序全部倒排列表
    // director/actor/fileType 的每个 QStringList 都必须按相同规则有序，后面的 set_intersection 依赖该前提。
    // <<< DEFENSE-NOTE | 本段讲解结束
    for (PostingIndex* index : {&loaded.directors_, &loaded.actors_, &loaded.fileTypes_}) {
        for (QStringList& ids : *index) {
            ids.sort(Qt::CaseSensitive);
        }
    }
    // >>> DEFENSE-NOTE | 重建步骤｜成功后整体替换
    // std::move 允许移动赋值接管新记录和索引。新输入未包含的旧记录被替换掉，不是追加合并。
    // <<< DEFENSE-NOTE | 本段讲解结束
    *this = std::move(loaded);
}

// >>> DEFENSE-NOTE | indexRecord = 将一条记录加入倒排表
// 导演/格式各一个桶，演员可对应多个桶。空字段不建索引。
// appendOnly=true 批量追加后统一排序；false 单条新增用 lower_bound 保持排序。
// <<< DEFENSE-NOTE | 本段讲解结束
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

// >>> DEFENSE-NOTE | addRecord = 增量新增
// 验证ID→规范化→更新 records_ 和倒排表。调用者应先完成持久化，再调用；这里不写硬盘。
// <<< DEFENSE-NOTE | 本段讲解结束
void MediaSearch::addRecord(const MediaRecord& record) {
    if (record.id.trimmed().isEmpty() || records_.contains(record.id)) {
        throw std::invalid_argument("Media record IDs must be nonempty and unique");
    }
    const MediaRecord entry = normalizedRecord(record);  // 增量更新沿用重建时的规范。
    records_.insert(entry.id, entry);
    indexRecord(entry, false);
}

// >>> DEFENSE-NOTE | removeRecord = 增量删除
// 先从 records_ 找原字段，再删导演/演员/格式桶里的ID，最后删记录；不存在抛 out_of_range。
// <<< DEFENSE-NOTE | 本段讲解结束
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
    // >>> DEFENSE-NOTE | 默认查询入口｜选择短列表优先
    // 单参数 search 委托带策略版本；三种策略匹配语义相同，只比较执行过程和耗时。
    // <<< DEFENSE-NOTE | 本段讲解结束
    return search(query, SearchStrategy::SmallestListFirst);
}

QStringList MediaSearch::search(const SearchQuery& query, SearchStrategy strategy,
                                SearchTrace* trace) const {
    if (trace) {
        *trace = {};
    }
    const SearchQuery filters = normalizedQuery(query);  // 两条执行路径共用过滤规则。
    // >>> DEFENSE-NOTE | 查询步骤 01｜线性扫描对照组
    // 遍历完整记录；任一非空条件不满足就 continue，因此条件是 AND。
    // 演员 contains 匹配完整姓名，不是模糊子串。结果最终排序，与索引路径一致。
    // <<< DEFENSE-NOTE | 本段讲解结束
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

    // >>> DEFENSE-NOTE | 查询步骤 02｜收集已填写条件的倒排列表
    // QHash 平均快速定位桶不等于整次查询 O(1)；之后仍有交集和输出成本。
    // 找不到的字段值对应空列表；留空条件不参加过滤。
    // <<< DEFENSE-NOTE | 本段讲解结束
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

    // >>> DEFENSE-NOTE | 查询步骤 03｜完全空条件的模块语义
    // 模块返回所有ID并排序；在线Qt入口可能另行要求至少一个搜索条件，两者层次不同。
    // <<< DEFENSE-NOTE | 本段讲解结束
    if (active.isEmpty()) {
        QStringList ids = records_.keys();  // 空查询返回全部 ID。
        ids.sort(Qt::CaseSensitive);
        return ids;
    }

    // >>> DEFENSE-NOTE | 查询步骤 04｜最短候选列表先用
    // 只比较已命中桶的长度，不是再扫描全库去统计。长度相同按字段枚举稳定排序。
    // <<< DEFENSE-NOTE | 本段讲解结束
    if (strategy == SearchStrategy::SmallestListFirst) {
        // 优先从候选少的条件开始，同样长时保留导演、演员、类型的固定顺序。
        std::sort(active.begin(), active.end(), [](const ActivePosting& left, const ActivePosting& right) {
            return left.ids->size() == right.ids->size()
                ? left.field < right.field : left.ids->size() < right.ids->size();
        });
    }

    // >>> DEFENSE-NOTE | 查询步骤 05｜有序交集与空集提前结束
    // 将第一列表作为候选，与后续列表用 set_intersection 求交。
    // 双指针前进，单次两列表交集 O(a+b)；候选为空就停止。trace 只记录过程用于性能对照。
    // <<< DEFENSE-NOTE | 本段讲解结束
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
