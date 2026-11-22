// >>> MAIN-DEFENSE | 主逻辑文件导读
// 可信资讯来源登记工具：解析来源配置、验证人物和HTTPS来源、幂等写入与分页。来源feed身份与具体文章地址分开。
// 阅读时先看函数入口说明，再按“处理阶段”往下；到每个return核对成功、空结果和错误是否区别。
// <<< MAIN-DEFENSE
#include "person_source_config.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMetaType>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace person_source_config {
using namespace care_contract;
using warehouse_contract::ErrorCode;
namespace {
constexpr qsizetype MaxJsonBytes = 2 * 1024 * 1024;
constexpr int MaxEntries = 10000;

// >>> MAIN-DEFENSE | 函数入口｜invalid
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：Status invalid(const QString& message) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Status invalid(const QString& message) { return {ErrorCode::InvalidArgument, message}; }
// >>> MAIN-DEFENSE | 函数入口｜error
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：Status error(const QString& message) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Status error(const QString& message) { return {ErrorCode::ServerError, message}; }
// >>> MAIN-DEFENSE | 函数入口｜validText
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：bool validText(const QString& text) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool validText(const QString& text) {
    if (text.simplified().isEmpty() || text.size() > MaxQueryUnits) return false;
    for (auto ch : text)
        if (ch.isNull() || ch.category() == QChar::Other_Control) return false;
    // >>> MAIN-DEFENSE | validText｜处理阶段 01
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return true;
}
// >>> MAIN-DEFENSE | 函数入口｜canonicalFeed
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：std::optional<QUrl> canonicalFeed(const QString& text) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
std::optional<QUrl> canonicalFeed(const QString& text) {
    if (text.size() > 2048) return std::nullopt;
    const QUrl url(text, QUrl::StrictMode);
    if (!url.isValid() || url.scheme() != "https" || url.host().isEmpty() ||
        !url.userInfo().isEmpty() || url.hasFragment())
        // >>> MAIN-DEFENSE | canonicalFeed｜处理阶段 01
        // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
        // <<< MAIN-DEFENSE
        return std::nullopt;
    return QUrl(url.toString(QUrl::FullyEncoded), QUrl::StrictMode);
}
// >>> MAIN-DEFENSE | 函数入口｜onlyKeys
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：bool onlyKeys(const QJsonObject& object, const QSet<QString>& allowed) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool onlyKeys(const QJsonObject& object, const QSet<QString>& allowed) {
    for (auto it = object.begin(); it != object.end(); ++it)
        if (!allowed.contains(it.key())) return false;
    return true;
}
// >>> MAIN-DEFENSE | 函数入口｜readDatabaseInteger
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：bool readDatabaseInteger(const QVariant& value, qint64& result) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool readDatabaseInteger(const QVariant& value, qint64& result) {
    if (value.isNull()) return false;
    const auto type = value.metaType().id();
    if (type != QMetaType::Int && type != QMetaType::UInt &&
        type != QMetaType::LongLong && type != QMetaType::ULongLong)
        // >>> MAIN-DEFENSE | readDatabaseInteger｜处理阶段 01
        // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
        // <<< MAIN-DEFENSE
        return false;
    bool ok = false;
    result = value.toLongLong(&ok);
    return ok;
}
// >>> MAIN-DEFENSE | 函数入口｜decodeSource
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：Result<Source> decodeSource(QSqlQuery& query) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Result<Source> decodeSource(QSqlQuery& query) {
    const auto personId = query.value(0).toString();
    const auto feedText = query.value(1).toString();
    const auto feed = canonicalFeed(feedText);
    std::optional<qint64> lastSuccess;
    qint64 stale = 0, generation = 0;
    // >>> MAIN-DEFENSE | decodeSource｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!validatePersonId(personId).ok() || !feed)
        return {error(QStringLiteral("Configured person source contains invalid identity or URL.")), {}};
    if (!query.value(2).isNull()) {
        qint64 parsed = 0;
        // >>> MAIN-DEFENSE | decodeSource｜处理阶段 02
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!readDatabaseInteger(query.value(2), parsed) || parsed < 0 || parsed >= MaxUtcExclusive)
            return {error(QStringLiteral("Configured person source has an invalid success time.")), {}};
        lastSuccess = parsed;
    }
    // >>> MAIN-DEFENSE | decodeSource｜处理阶段 03
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!readDatabaseInteger(query.value(3), stale) || (stale != 0 && stale != 1) ||
        !readDatabaseInteger(query.value(4), generation) || generation < 0)
        return {error(QStringLiteral("Configured person source has invalid refresh state.")), {}};
    return {{}, Source{personId, *feed, lastSuccess, stale == 1, generation}};
}
// >>> MAIN-DEFENSE | 函数入口｜readPage
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：Result<Page<Source>> readPage(QSqlDatabase& db, const QString& personId,                                const PageRequest& page) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Result<Page<Source>> readPage(QSqlDatabase& db, const QString& personId,
                              const PageRequest& page) {
    if (!db.isOpen() || db.driverName() != "QSQLITE")
        return {error(QStringLiteral("Person source database is unavailable.")), {}};
    const auto status = validate(page);
    // >>> MAIN-DEFENSE | readPage｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!status.ok()) return {status, {}};
    if (!personId.isEmpty()) {
        const auto idStatus = validatePersonId(personId);
        if (!idStatus.ok()) return {idStatus, {}};
        QSqlQuery exists(db);
        // >>> MAIN-DEFENSE | readPage｜处理阶段 02
        // 准备参数化SQL，涉及 person_catalog。随后把实际参数绑定到问号，执行并检查错误；prepare本身尚未完成读写。
        // <<< MAIN-DEFENSE
        exists.prepare("SELECT 1 FROM person_catalog WHERE person_id=?");
        exists.addBindValue(personId);
        if (!exists.exec())
            return {error(QStringLiteral("Cannot verify configured person.")), {}};
        // >>> MAIN-DEFENSE | readPage｜处理阶段 03
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!exists.next())
            return {{ErrorCode::NotFound, QStringLiteral("Configured person does not exist.")}, {}};
    }
    QSqlQuery query(db);
    // >>> MAIN-DEFENSE | readPage｜处理阶段 04
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (personId.isEmpty()) {
        query.prepare("SELECT person_id,source_url,last_success_utc,stale,refresh_generation "
                      "FROM person_sources ORDER BY person_id,source_url LIMIT ? OFFSET ?");
        query.addBindValue(page.limit + 1);
        query.addBindValue(page.offset);
    } else {
        // >>> MAIN-DEFENSE | readPage｜处理阶段 05
        // 准备参数化SQL，涉及 person_sources。随后把实际参数绑定到问号，执行并检查错误；prepare本身尚未完成读写。
        // <<< MAIN-DEFENSE
        query.prepare("SELECT person_id,source_url,last_success_utc,stale,refresh_generation "
                      "FROM person_sources WHERE person_id=? "
                      "ORDER BY person_id,source_url LIMIT ? OFFSET ?");
        query.addBindValue(personId);
        query.addBindValue(page.limit + 1);
        query.addBindValue(page.offset);
    }
    // >>> MAIN-DEFENSE | readPage｜处理阶段 06
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!query.exec())
        return {error(QStringLiteral("Cannot read configured person sources.")), {}};
    Page<Source> result;
    while (query.next()) {
        // >>> MAIN-DEFENSE | readPage｜处理阶段 07
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (result.items.size() == page.limit) {
            result.nextOffset = page.offset + page.limit;
            break;
        }
        const auto source = decodeSource(query);
        // >>> MAIN-DEFENSE | readPage｜处理阶段 08
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!source.status.ok() || !source.value) return {source.status, {}};
        result.items.append(*source.value);
    }
    if (query.lastError().isValid())
        // >>> MAIN-DEFENSE | readPage｜处理阶段 09
        // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
        // <<< MAIN-DEFENSE
        return {error(QStringLiteral("Reading configured person sources failed.")), {}};
    return {{}, result};
}
class Savepoint {
public:
    // >>> MAIN-DEFENSE | 函数入口｜Savepoint
    // 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
    // 签名提示：explicit Savepoint(QSqlDatabase& db)          : db_(db), name_(QStringLiteral("person_source_") +                           QUuid::createUuid().toString(QUuid::Id128)) 
    // 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
    // <<< MAIN-DEFENSE
    explicit Savepoint(QSqlDatabase& db)
        : db_(db), name_(QStringLiteral("person_source_") +
                         QUuid::createUuid().toString(QUuid::Id128)) {}
    // >>> MAIN-DEFENSE | 函数入口｜start
    // 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
    // 签名提示：bool start() 
    // 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
    // <<< MAIN-DEFENSE
    bool start() {
        QSqlQuery query(db_);
        active_ = query.exec("SAVEPOINT " + name_);
        return active_;
    }
    // >>> MAIN-DEFENSE | 函数入口｜release
    // 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
    // 签名提示：bool release() 
    // 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
    // <<< MAIN-DEFENSE
    bool release() {
        QSqlQuery query(db_);
        if (!query.exec("RELEASE SAVEPOINT " + name_)) return false;
        active_ = false;
        // >>> MAIN-DEFENSE | release｜处理阶段 01
        // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
        // <<< MAIN-DEFENSE
        return true;
    }
    // >>> MAIN-DEFENSE | 函数入口｜~Savepoint
    // 析构收尾：释放本对象持有的连接/请求等资源；注意Qt回调与数据库句柄的生命周期。
    // 签名提示：~Savepoint() 
    // 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
    // <<< MAIN-DEFENSE
    ~Savepoint() {
        if (!active_) return;
        QSqlQuery query(db_);
        query.exec("ROLLBACK TO SAVEPOINT " + name_);
        query.exec("RELEASE SAVEPOINT " + name_);
    }
private:
    QSqlDatabase& db_;
    QString name_;
    bool active_ = false;
};
}

// >>> MAIN-DEFENSE | 函数入口｜parse
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：Result<Batch> parse(const QByteArray& json) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Result<Batch> parse(const QByteArray& json) {
    if (json.size() > MaxJsonBytes)
        return {invalid(QStringLiteral("Person source configuration exceeds 2 MiB.")), {}};
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json, &parseError);
    // >>> MAIN-DEFENSE | parse｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return {invalid(QStringLiteral("Person source configuration must be a JSON object.")), {}};
    const auto root = document.object();
    if (!onlyKeys(root, {"schema", "provenance", "sources"}) ||
        root.value("schema") != QJsonValue("care-person-source-config-v1") ||
        !root.value("provenance").isString() ||
        !validText(root.value("provenance").toString()) ||
        !root.value("sources").isArray())
        // >>> MAIN-DEFENSE | parse｜处理阶段 02
        // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
        // <<< MAIN-DEFENSE
        return {invalid(QStringLiteral("Person source configuration has an invalid root.")), {}};
    const auto rows = root.value("sources").toArray();
    if (rows.size() > MaxEntries)
        return {invalid(QStringLiteral("Person source configuration has too many entries.")), {}};
    Batch batch;
    batch.provenance = root.value("provenance").toString();
    // >>> MAIN-DEFENSE | parse｜处理阶段 03
    // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
    // <<< MAIN-DEFENSE
    for (const auto& value : rows) {
        if (!value.isObject())
            return {invalid(QStringLiteral("Person source entry must be an object.")), {}};
        const auto object = value.toObject();
        // >>> MAIN-DEFENSE | parse｜处理阶段 04
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!onlyKeys(object, {"person_id", "feed_url"}) ||
            !object.value("person_id").isString() || !object.value("feed_url").isString())
            return {invalid(QStringLiteral("Person source entry has invalid fields or types.")), {}};
        const auto personId = object.value("person_id").toString();
        const auto idStatus = validatePersonId(personId);
        // >>> MAIN-DEFENSE | parse｜处理阶段 05
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!idStatus.ok()) return {invalid(QStringLiteral("Person source person_id is invalid.")), {}};
        const auto feed = canonicalFeed(object.value("feed_url").toString());
        if (!feed)
            return {invalid(QStringLiteral("Person source feed_url must be a credential-free HTTPS URL without a fragment.")), {}};
        batch.entries.append({personId, *feed});
    }
    // >>> MAIN-DEFENSE | parse｜处理阶段 06
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return {{}, batch};
}

// >>> MAIN-DEFENSE | 函数入口｜registerBatch
// 验证可信来源配置并在数据库登记，保持可重复运行，不接受客户端任意抓取地址。
// 签名提示：Result<Report> registerBatch(QSqlDatabase& db, const Batch& batch) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Result<Report> registerBatch(QSqlDatabase& db, const Batch& batch) {
    if (!validText(batch.provenance) || batch.entries.size() > MaxEntries)
        return {invalid(QStringLiteral("Person source batch is invalid or too large.")), {}};
    if (!db.isOpen() || db.driverName() != "QSQLITE")
        // >>> MAIN-DEFENSE | registerBatch｜处理阶段 01
        // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
        // <<< MAIN-DEFENSE
        return {error(QStringLiteral("Person source database is unavailable.")), {}};
    Savepoint savepoint(db);
    if (!savepoint.start())
        return {error(QStringLiteral("Cannot start person source registration.")), {}};
    Report report;
    // >>> MAIN-DEFENSE | registerBatch｜处理阶段 02
    // 准备本步骤的内存查找、去重或汇总容器；它们不是数据库表，生命周期通常只在本次调用内。
    // <<< MAIN-DEFENSE
    QSet<QString> seen;
    for (const auto& entry : batch.entries) {
        const auto idStatus = validatePersonId(entry.personId);
        const auto feed = canonicalFeed(entry.feedUrl.toString());
        // >>> MAIN-DEFENSE | registerBatch｜处理阶段 03
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!idStatus.ok()) return {invalid(QStringLiteral("Person source person_id is invalid.")), {}};
        if (!feed) return {invalid(QStringLiteral("Person source feed_url is invalid.")), {}};
        const auto key = entry.personId + QChar(0x1f) + feed->toString();
        if (seen.contains(key)) { ++report.unchanged; continue; }
        seen.insert(key);

        QSqlQuery person(db);
        // >>> MAIN-DEFENSE | registerBatch｜处理阶段 04
        // 准备参数化SQL，涉及 person_catalog。随后把实际参数绑定到问号，执行并检查错误；prepare本身尚未完成读写。
        // <<< MAIN-DEFENSE
        person.prepare("SELECT 1 FROM person_catalog WHERE person_id=?");
        person.addBindValue(entry.personId);
        if (!person.exec())
            return {error(QStringLiteral("Cannot verify person source identity.")), {}};
        // >>> MAIN-DEFENSE | registerBatch｜处理阶段 05
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!person.next())
            return {{ErrorCode::NotFound, QStringLiteral("Person source references an unknown person.")}, {}};

        QSqlQuery existing(db);
        // >>> MAIN-DEFENSE | registerBatch｜处理阶段 06
        // 准备参数化SQL，涉及 person_sources。随后把实际参数绑定到问号，执行并检查错误；prepare本身尚未完成读写。
        // <<< MAIN-DEFENSE
        existing.prepare("SELECT 1 FROM person_sources WHERE person_id=? AND source_url=?");
        existing.addBindValue(entry.personId);
        existing.addBindValue(feed->toString());
        if (!existing.exec())
            // >>> MAIN-DEFENSE | registerBatch｜处理阶段 07
            // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
            // <<< MAIN-DEFENSE
            return {error(QStringLiteral("Cannot check person source configuration.")), {}};
        if (existing.next()) { ++report.unchanged; continue; }

        QSqlQuery insert(db);
        // >>> MAIN-DEFENSE | registerBatch｜处理阶段 08
        // 准备参数化SQL，涉及 person_sources。随后把实际参数绑定到问号，执行并检查错误；prepare本身尚未完成读写。
        // <<< MAIN-DEFENSE
        insert.prepare("INSERT INTO person_sources"
                       "(person_id,source_url,last_success_utc,stale,refresh_generation) "
                       "VALUES(?,?,NULL,0,0)");
        insert.addBindValue(entry.personId);
        insert.addBindValue(feed->toString());
        // >>> MAIN-DEFENSE | registerBatch｜处理阶段 09
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!insert.exec())
            return {error(QStringLiteral("Cannot register person source configuration.")), {}};
        ++report.inserted;
    }
    // >>> MAIN-DEFENSE | registerBatch｜处理阶段 10
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!savepoint.release())
        return {error(QStringLiteral("Cannot commit person source registration.")), {}};
    return {{}, report};
}

// >>> MAIN-DEFENSE | 函数入口｜registerFile
// 读取有大小限制的来源配置文件，再委托parse和registerBatch。
// 签名提示：Result<Report> registerFile(QSqlDatabase& db, const QString& path) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Result<Report> registerFile(QSqlDatabase& db, const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > MaxJsonBytes)
        return {invalid(QStringLiteral("Cannot read person source configuration, or it exceeds 2 MiB.")), {}};
    // >>> MAIN-DEFENSE | registerFile｜处理阶段 01
    // 读取已经打开或收到的数据，后续仍须解析并验证格式；读到字节不代表业务内容合法。
    // <<< MAIN-DEFENSE
    const auto batch = parse(file.readAll());
    if (!batch.status.ok() || !batch.value) return {batch.status, {}};
    return registerBatch(db, *batch.value);
}

// >>> MAIN-DEFENSE | 函数入口｜listSources
// 对已登记资讯源分页，返回来源身份和刷新状态。
// 签名提示：Result<Page<Source>> listSources(QSqlDatabase& db, const PageRequest& page) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Result<Page<Source>> listSources(QSqlDatabase& db, const PageRequest& page) {
    return readPage(db, {}, page);
}

// >>> MAIN-DEFENSE | 函数入口｜listSourcesForPerson
// 读取指定人物来源分页，来源URL与具体文章URL含义不同。
// 签名提示：Result<Page<Source>> listSourcesForPerson(      QSqlDatabase& db, const QString& personId, const PageRequest& page) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Result<Page<Source>> listSourcesForPerson(
    QSqlDatabase& db, const QString& personId, const PageRequest& page) {
    if (personId.isEmpty())
        return {invalid(QStringLiteral("Person identifier is required.")), {}};
    // >>> MAIN-DEFENSE | listSourcesForPerson｜处理阶段 01
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return readPage(db, personId, page);
}
} // namespace person_source_config
