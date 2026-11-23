// >>> MAIN-DEFENSE | 主逻辑文件导读
// 单机业务协调：仓储负责磁盘，records_是内存记录，search_是检索索引。先完成保存，再维护内存与索引；若后续失败，必须区分已持久化和未保存。
// 阅读时先看函数入口说明，再按“处理阶段”往下；到每个return核对成功、空结果和错误是否区别。
// <<< MAIN-DEFENSE
#include "medialibrary.h"
#include <algorithm>
#include <exception>
#include <utility>

namespace {
// >>> MAIN-DEFENSE | 函数入口｜sameRecord
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：bool sameRecord(const MediaRecord& a, const MediaRecord& b) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool sameRecord(const MediaRecord& a, const MediaRecord& b) {
    return a.id == b.id && a.fileName == b.fileName && a.fileType == b.fileType &&
           a.fileSize == b.fileSize && a.director == b.director && a.actors == b.actors &&
           a.viewCount == b.viewCount && a.releaseDate == b.releaseDate && a.releasePeriod == b.releasePeriod &&
           a.awards == b.awards && a.category == b.category;
}
}

// >>> MAIN-DEFENSE | 函数入口｜MediaLibrary::MediaLibrary
// 构造依赖与初始状态；参数通过初始化列表保存，本段不能当成登录或数据加载已经完成。
// 签名提示：MediaLibrary::MediaLibrary(QString path, QString password)
//       : repository_(std::move(path)), password_(std::move(password))
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
MediaLibrary::MediaLibrary(QString path, QString password)
    : repository_(std::move(path)), password_(std::move(password)) {}

// >>> MAIN-DEFENSE | 函数入口｜MediaLibrary::load
// 从仓储读取并验证数据；上层在全部加载成功后才采用新快照，避免半份结果。
// 签名提示：RepositoryResult MediaLibrary::load() 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
RepositoryResult MediaLibrary::load() {
    QList<MediaRecord> next;
    auto result = repository_.load(next);
    if (!result.success) return result;
    result = repository_.ensureSequence(next);
    // >>> MAIN-DEFENSE | load｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!result.success) return result;
    try { search_.rebuild(next); }
    catch (const std::exception& e) { return {false, QString::fromUtf8(e.what())}; }
    records_ = std::move(next);
    loaded_ = true;
    // >>> MAIN-DEFENSE | load｜处理阶段 02
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return {true, {}};
}

// >>> MAIN-DEFENSE | 函数入口｜MediaLibrary::addNew
// 为新媒体预留ID，再调用保存流程；不按当前列表行号充当稳定编号。
// 签名提示：RepositoryResult MediaLibrary::addNew(MediaRecord& record) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
RepositoryResult MediaLibrary::addNew(MediaRecord& record) {
    if (!loaded_) return {false, QStringLiteral("请先加载数据")};
    auto result = verifyUnchanged();
    if (!result.success) return result;
    QString id;
    result = repository_.reserveNextId(id);
    // >>> MAIN-DEFENSE | addNew｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!result.success) return result;
    record.id = id;
    return persistRecord(record);
}

// >>> MAIN-DEFENSE | 函数入口｜MediaLibrary::add
// 在现有记录中添加已校验记录；上层addNew负责分配ID。
// 签名提示：RepositoryResult MediaLibrary::add(const MediaRecord& record) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
RepositoryResult MediaLibrary::add(const MediaRecord& record) {
    MediaRecord copy = record;
    return addNew(copy);
}

// >>> MAIN-DEFENSE | 函数入口｜MediaLibrary::persistRecord
// 先调用仓储写入，成功后更新search_与records_；索引失败时尝试恢复，并返回已保存标志，防界面重复新增。
// 签名提示：RepositoryResult MediaLibrary::persistRecord(const MediaRecord& record) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
RepositoryResult MediaLibrary::persistRecord(const MediaRecord& record) {
    if (!loaded_) return {false, QStringLiteral("请先加载数据")};
    auto check = verifyUnchanged();
    if (!check.success) return check;
    auto result = repository_.add(record);
    // >>> MAIN-DEFENSE | persistRecord｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!result.success) return result;
    try { search_.addRecord(record); records_.append(record); return {true, {}, true}; }
    catch (const std::exception&) {
        loaded_ = false;
        auto recovery = load();
        // >>> MAIN-DEFENSE | persistRecord｜处理阶段 02
        // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
        // <<< MAIN-DEFENSE
        return {false, recovery.success ? QStringLiteral("数据已保存，索引已重建，请刷新界面") :
                       QStringLiteral("数据已保存，索引重建失败，请重新加载：") + recovery.error, true};
    }
}

// >>> MAIN-DEFENSE | 函数入口｜MediaLibrary::remove
// 按ID删除媒体；业务路径先持久化，再同步内存和检索索引。
// 签名提示：RepositoryResult MediaLibrary::remove(const QString& id) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
RepositoryResult MediaLibrary::remove(const QString& id) {
    if (!loaded_) return {false, QStringLiteral("请先加载数据")};
    auto check = verifyUnchanged();
    if (!check.success) return check;
    auto result = repository_.remove(id);
    // >>> MAIN-DEFENSE | remove｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!result.success) return result;
    try {
        search_.removeRecord(id);
        for (qsizetype i = 0; i < records_.size(); ++i)
            // >>> MAIN-DEFENSE | remove｜处理阶段 02
            // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
            // <<< MAIN-DEFENSE
            if (records_[i].id == id) { records_.removeAt(i); break; }
        return {true, {}, true};
    } catch (const std::exception&) {
        loaded_ = false;
        auto recovery = load();
        // >>> MAIN-DEFENSE | remove｜处理阶段 03
        // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
        // <<< MAIN-DEFENSE
        return {false, recovery.success ? QStringLiteral("数据已删除，索引已重建，请刷新界面") :
                       QStringLiteral("数据已删除，索引重建失败，请重新加载：") + recovery.error, true};
    }
}

// >>> MAIN-DEFENSE | 函数入口｜MediaLibrary::verifyUnchanged
// 检查当前文件与内存快照是否一致，发现外部变动时阻止盲目覆盖。
// 签名提示：RepositoryResult MediaLibrary::verifyUnchanged() 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
RepositoryResult MediaLibrary::verifyUnchanged() {
    QList<MediaRecord> current;
    auto result = repository_.load(current);
    if (!result.success) return result;
    // >>> MAIN-DEFENSE | verifyUnchanged｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (current.size() != records_.size()) { loaded_ = false; return {false, QStringLiteral("CSV 已被外部修改，请重新加载")}; }
    for (qsizetype i = 0; i < current.size(); ++i)
        if (!sameRecord(current.at(i), records_.at(i))) { loaded_ = false; return {false, QStringLiteral("CSV 已被外部修改，请重新加载")}; }
    return {true, {}};
}

// >>> MAIN-DEFENSE | 函数入口｜MediaLibrary::authenticate
// 校验单机媒体库口令；与在线socket登录不是同一个入口。
// 签名提示：bool MediaLibrary::authenticate(const QString& password) const 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool MediaLibrary::authenticate(const QString& password) const {
    return !password_.isEmpty() && password == password_;
}
// >>> MAIN-DEFENSE | 函数入口｜MediaLibrary::fileTypes
// 收集当前库实际存在的格式，供过滤选项使用；不同于全部允许格式白名单。
// 签名提示：QStringList MediaLibrary::fileTypes() const 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
QStringList MediaLibrary::fileTypes() const {
    QStringList result;
    if (!loaded_) return result;
    for (const auto& r : records_) if (!result.contains(r.fileType)) result.append(r.fileType);
    result.sort(); return result;
}
// >>> MAIN-DEFENSE | 函数入口｜MediaLibrary::supportedFileTypes
// 提供统一音视频扩展名白名单；Qt可复用，不再维护第二份不一致名单。
// 签名提示：QStringList MediaLibrary::supportedFileTypes() 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
QStringList MediaLibrary::supportedFileTypes() { return CsvMediaRepository::supportedFileTypes(); }
// >>> MAIN-DEFENSE | 函数入口｜MediaLibrary::records
// 读取当前内存记录并按可选类型筛选，不从Qt表格文字反向解析数据。
// 签名提示：QList<MediaRecord> MediaLibrary::records(const QString& fileType) const 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
QList<MediaRecord> MediaLibrary::records(const QString& fileType) const {
    QList<MediaRecord> result;
    if (!loaded_) return result;
    for (const auto& r : records_)
        // >>> MAIN-DEFENSE | records｜处理阶段 01
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (fileType.isEmpty() || r.fileType.compare(fileType, Qt::CaseInsensitive) == 0) result.append(r);
    std::sort(result.begin(), result.end(), [](const MediaRecord& a, const MediaRecord& b) {
        const auto left = mediaReleaseText(a);
        const auto right = mediaReleaseText(b);
        // >>> MAIN-DEFENSE | records｜处理阶段 02
        // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
        // <<< MAIN-DEFENSE
        return left == right ? a.id < b.id : left > right;
    });
    return result;
}
// >>> MAIN-DEFENSE | 函数入口｜MediaLibrary::find
// 按稳定媒体ID查当前记录，成功通过输出参数返回资料。
// 签名提示：bool MediaLibrary::find(const QString& id, MediaRecord& result) const 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool MediaLibrary::find(const QString& id, MediaRecord& result) const {
    if (!loaded_) return false;
    for (const auto& r : records_) if (r.id == id) { result = r; return true; }
    return false;
}
// >>> MAIN-DEFENSE | 函数入口｜MediaLibrary::search
// 委托MediaSearch执行精确检索，业务层不重新实现倒排交集算法。
// 签名提示：QStringList MediaLibrary::search(const SearchQuery& query) const 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
QStringList MediaLibrary::search(const SearchQuery& query) const {
    return loaded_ ? search_.search(query) : QStringList{};
}
// >>> MAIN-DEFENSE | 函数入口｜MediaLibrary::search
// 委托MediaSearch执行精确检索，业务层不重新实现倒排交集算法。
// 签名提示：QStringList MediaLibrary::search(const SearchQuery& query, SearchTrace& trace) const 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
QStringList MediaLibrary::search(const SearchQuery& query, SearchTrace& trace) const {
    if (!loaded_) {
        trace = {};
        return {};
    }
    // >>> MAIN-DEFENSE | search｜处理阶段 01
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return search_.search(query, SearchStrategy::SmallestListFirst, &trace);
}
