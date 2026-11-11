#include "medialibrary.h"
#include <algorithm>
#include <exception>
#include <utility>

namespace {
bool sameRecord(const MediaRecord& a, const MediaRecord& b) {
    return a.id == b.id && a.fileName == b.fileName && a.fileType == b.fileType &&
           a.fileSize == b.fileSize && a.director == b.director && a.actors == b.actors &&
           a.viewCount == b.viewCount && a.releaseDate == b.releaseDate &&
           a.awards == b.awards && a.category == b.category;
}
}

MediaLibrary::MediaLibrary(QString path, QString password)
    : repository_(std::move(path)), password_(std::move(password)) {}

RepositoryResult MediaLibrary::load() {
    QList<MediaRecord> next;
    auto result = repository_.load(next);
    if (!result.success) return result;
    try { search_.rebuild(next); }
    catch (const std::exception& e) { return {false, QString::fromUtf8(e.what())}; }
    records_ = std::move(next);
    loaded_ = true;
    return {true, {}};
}

RepositoryResult MediaLibrary::add(const MediaRecord& record) {
    if (!loaded_) return {false, QStringLiteral("请先加载数据")};
    auto check = verifyUnchanged();
    if (!check.success) return check;
    auto result = repository_.add(record);
    if (!result.success) return result;
    try { search_.addRecord(record); records_.append(record); return {true, {}}; }
    catch (const std::exception&) {
        loaded_ = false;
        auto recovery = load();
        return {false, recovery.success ? QStringLiteral("数据已保存，索引已重建，请刷新界面") :
                       QStringLiteral("数据已保存，索引重建失败，请重新加载：") + recovery.error};
    }
}

RepositoryResult MediaLibrary::remove(const QString& id) {
    if (!loaded_) return {false, QStringLiteral("请先加载数据")};
    auto check = verifyUnchanged();
    if (!check.success) return check;
    auto result = repository_.remove(id);
    if (!result.success) return result;
    try {
        search_.removeRecord(id);
        for (qsizetype i = 0; i < records_.size(); ++i)
            if (records_[i].id == id) { records_.removeAt(i); break; }
        return {true, {}};
    } catch (const std::exception&) {
        loaded_ = false;
        auto recovery = load();
        return {false, recovery.success ? QStringLiteral("数据已删除，索引已重建，请刷新界面") :
                       QStringLiteral("数据已删除，索引重建失败，请重新加载：") + recovery.error};
    }
}

RepositoryResult MediaLibrary::incrementViewCount(const QString& id, quint64& count) {
    if (!loaded_)
        return {false, QStringLiteral("请先加载数据")};
    for (auto& record : records_) {
        if (record.id != id)
            continue;
        const auto saved = repository_.incrementViewCount(id, count);
        // The search index matches director, actor and type, so it needs no update.
        if (saved.success)
            record.viewCount = count;
        return saved;
    }
    return {false, QStringLiteral("未找到该 ID")};
}

RepositoryResult MediaLibrary::verifyUnchanged() {
    QList<MediaRecord> current;
    auto result = repository_.load(current);
    if (!result.success) return result;
    if (current.size() != records_.size()) { loaded_ = false; return {false, QStringLiteral("CSV 已被外部修改，请重新加载")}; }
    for (qsizetype i = 0; i < current.size(); ++i)
        if (!sameRecord(current.at(i), records_.at(i))) { loaded_ = false; return {false, QStringLiteral("CSV 已被外部修改，请重新加载")}; }
    return {true, {}};
}

bool MediaLibrary::authenticate(const QString& password) const {
    return !password_.isEmpty() && password == password_;
}
QStringList MediaLibrary::fileTypes() const {
    QStringList result;
    if (!loaded_) return result;
    for (const auto& r : records_) if (!result.contains(r.fileType)) result.append(r.fileType);
    result.sort(); return result;
}
QList<MediaRecord> MediaLibrary::records(const QString& fileType) const {
    QList<MediaRecord> result;
    if (!loaded_) return result;
    for (const auto& r : records_)
        if (fileType.isEmpty() || r.fileType.compare(fileType, Qt::CaseInsensitive) == 0) result.append(r);
    std::sort(result.begin(), result.end(), [](const MediaRecord& a, const MediaRecord& b) {
        return a.releaseDate == b.releaseDate ? a.id < b.id : a.releaseDate > b.releaseDate;
    });
    return result;
}
bool MediaLibrary::find(const QString& id, MediaRecord& result) const {
    if (!loaded_) return false;
    for (const auto& r : records_) if (r.id == id) { result = r; return true; }
    return false;
}
QStringList MediaLibrary::search(const SearchQuery& query) const {
    return loaded_ ? search_.search(query) : QStringList{};
}
QStringList MediaLibrary::search(const SearchQuery& query, SearchTrace& trace) const {
    if (!loaded_) {
        trace = {};
        return {};
    }
    return search_.search(query, SearchStrategy::SmallestListFirst, &trace);
}
