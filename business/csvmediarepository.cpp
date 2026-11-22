// >>> MAIN-DEFENSE | 主逻辑文件导读
// 单机CSV持久化：状态机解析CSV，演员/奖项字段用JSON数组编码；ID序号单独维护，保存采用QSaveFile。此模块不控制在线仓库权限。
// 阅读时先看函数入口说明，再按“处理阶段”往下；到每个return核对成功、空结果和错误是否区别。
// <<< MAIN-DEFENSE
#include "csvmediarepository.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <utility>
#include <limits>

namespace {
// >>> MAIN-DEFENSE | 函数入口｜fail
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：RepositoryResult fail(const QString& message) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
RepositoryResult fail(const QString& message) { return {false, message}; }
const QStringList header = {"id", "file_name", "file_type", "file_size", "director", "actors",
                            "view_count", "release_date", "awards", "category"};

// 将 CSV 解析为行；引号、逗号、换行和 CRLF 均按 CSV 规则处理。
// >>> MAIN-DEFENSE | 函数入口｜parseCsv
// CSV状态机：识别普通字段、引号字段、结束引号和行结束；逗号在引号内部不是分隔符。
// 签名提示：bool parseCsv(const QString& text, QList<QStringList>& rows) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool parseCsv(const QString& text, QList<QStringList>& rows) {
    QString field; QStringList row;
    enum State { Start, Plain, Quoted, Closed } state = Start;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        // >>> MAIN-DEFENSE | parseCsv｜处理阶段 01
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (state == Quoted) {
            if (c == '"') {
                if (i + 1 < text.size() && text.at(i+1) == '"') { field += c; ++i; }
                else state = Closed;
            } else field += c;
            continue;
        }
        // >>> MAIN-DEFENSE | parseCsv｜处理阶段 02
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (c == ',') { row.append(field); field.clear(); state = Start; continue; }
        if (c == '\r' || c == '\n') {
            if (c == '\r' && i + 1 < text.size() && text.at(i+1) == '\n') ++i;
            row.append(field); field.clear(); rows.append(row); row.clear(); state = Start; continue;
        }
        // >>> MAIN-DEFENSE | parseCsv｜处理阶段 03
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (state == Closed) return false;
        if (c == '"') { if (state != Start) return false; state = Quoted; }
        else { field += c; state = Plain; }
    }
    // >>> MAIN-DEFENSE | parseCsv｜处理阶段 04
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (state == Quoted) return false;
    if (state != Start || !row.isEmpty()) { row.append(field); rows.append(row); }
    return true;
}

// >>> MAIN-DEFENSE | 函数入口｜parseList
// 将CSV内的JSON数组还原成字符串列表，验证每项类型及空值；演员姓名包含逗号不靠逗号拆分。
// 签名提示：bool parseList(const QString& value, QStringList& result) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool parseList(const QString& value, QStringList& result) {
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(value.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isArray()) return false;
    // >>> MAIN-DEFENSE | parseList｜处理阶段 01
    // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
    // <<< MAIN-DEFENSE
    for (const auto& item : doc.array()) {
        if (!item.isString() || item.toString().trimmed().isEmpty()) return false;
        result.append(item.toString());
    }
    // >>> MAIN-DEFENSE | parseList｜处理阶段 02
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return true;
}
// >>> MAIN-DEFENSE | 函数入口｜parseNumber
// 读取非负整数，拒绝非数字及溢出，不允许用负数表示大小或浏览次数。
// 签名提示：bool parseNumber(const QString& value, quint64& result) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool parseNumber(const QString& value, quint64& result) {
    if (value.isEmpty()) return false;
    for (const auto c : value) if (c < '0' || c > '9') return false;
    bool ok = false; result = value.toULongLong(&ok); return ok;
}
// >>> MAIN-DEFENSE | 函数入口｜validRecord
// 校验媒体ID、文件名、类型、日期/发行期间和多值字段；缺失日期与非法日期必须区分。
// 签名提示：bool validRecord(const MediaRecord& r) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool validRecord(const MediaRecord& r) {
    if (r.id.isEmpty() || r.id != r.id.trimmed() || r.fileName.trimmed().isEmpty() ||
        (!r.fileType.isEmpty() && !CsvMediaRepository::supportedFileTypes().contains(r.fileType))) return false;
    if (!r.releasePeriod.isEmpty() && (!r.releaseDate.isNull() || !validReleasePeriod(r.releasePeriod)))
        // >>> MAIN-DEFENSE | validRecord｜处理阶段 01
        // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
        // <<< MAIN-DEFENSE
        return false;
    if (!r.releaseDate.isValid()) {
        if (!r.releaseDate.isNull()) return false;
    } else {
        // ISO 日期必须能以四位年格式往返，否则写入后无法重新加载。
        const QString dateText = r.releaseDate.toString(Qt::ISODate);
        // >>> MAIN-DEFENSE | validRecord｜处理阶段 02
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (dateText.size() != 10 || QDate::fromString(dateText, Qt::ISODate) != r.releaseDate) return false;
    }
    for (const auto& s : r.actors) if (s.trimmed().isEmpty()) return false;
    for (const auto& s : r.awards) if (s.trimmed().isEmpty()) return false;
    // >>> MAIN-DEFENSE | validRecord｜处理阶段 03
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return true;
}
// >>> MAIN-DEFENSE | 函数入口｜quoted
// 按CSV规则转义双引号并包裹字段，防逗号或换行破坏列结构。
// 签名提示：QString quoted(QString value) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
QString quoted(QString value) {
    value.replace('"', "\"\"");
    return '"' + value + '"';
}
// >>> MAIN-DEFENSE | 函数入口｜listJson
// 将演员或奖项列表编码成JSON数组文本，再作为一个CSV字段保存。
// 签名提示：QString listJson(const QStringList& values) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
QString listJson(const QStringList& values) {
    QJsonArray a; for (const auto& value : values) a.append(value);
    return QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact));
}
}

// >>> MAIN-DEFENSE | 函数入口｜CsvMediaRepository::CsvMediaRepository
// 构造依赖与初始状态；参数通过初始化列表保存，本段不能当成登录或数据加载已经完成。
// 签名提示：CsvMediaRepository::CsvMediaRepository(QString path) : path_(std::move(path)) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
CsvMediaRepository::CsvMediaRepository(QString path) : path_(std::move(path)) {}

// >>> MAIN-DEFENSE | 函数入口｜CsvMediaRepository::supportedFileTypes
// 提供统一音视频扩展名白名单；Qt可复用，不再维护第二份不一致名单。
// 签名提示：QStringList CsvMediaRepository::supportedFileTypes() 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
QStringList CsvMediaRepository::supportedFileTypes() {
    return {"AAC", "AVI", "FLAC", "FLV", "M4A", "MKV", "MOV", "MP3", "MP4", "OGG", "WAV", "WEBM", "WMV"};
}

// >>> MAIN-DEFENSE | 函数入口｜CsvMediaRepository::validateNewRecord
// 校验新增记录的类型与字段，ID由业务层分配；验证不会直接写磁盘。
// 签名提示：RepositoryResult CsvMediaRepository::validateNewRecord(const MediaRecord& record) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
RepositoryResult CsvMediaRepository::validateNewRecord(const MediaRecord& record) {
    if (!supportedFileTypes().contains(record.fileType))
        return fail(QStringLiteral("不支持的文件类型，请选择支持的音视频扩展名"));
    MediaRecord candidate = record;
    candidate.id = QStringLiteral("M000001"); // 新增时 ID 由业务层分配。
    // >>> MAIN-DEFENSE | validateNewRecord｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!validRecord(candidate)) return fail(QStringLiteral("记录字段无效"));
    return {true, {}};
}

// >>> MAIN-DEFENSE | 函数入口｜CsvMediaRepository::ensureSequence
// 确保编号序列元数据与已有记录衔接，删除不重新编号。
// 签名提示：RepositoryResult CsvMediaRepository::ensureSequence(const QList<MediaRecord>& records) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
RepositoryResult CsvMediaRepository::ensureSequence(const QList<MediaRecord>& records) {
    return updateSequence(records, false, nullptr);
}

// >>> MAIN-DEFENSE | 函数入口｜CsvMediaRepository::reserveNextId
// 预留后续唯一媒体ID，不能简单使用记录数+1，否则删除后会冲突。
// 签名提示：RepositoryResult CsvMediaRepository::reserveNextId(QString& id) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
RepositoryResult CsvMediaRepository::reserveNextId(QString& id) {
    QList<MediaRecord> records;
    auto result = load(records);
    if (!result.success) return result;
    // >>> MAIN-DEFENSE | reserveNextId｜处理阶段 01
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return updateSequence(records, true, &id);
}

// >>> MAIN-DEFENSE | 函数入口｜CsvMediaRepository::updateSequence
// 读取并锁定序号状态，结合已有编号更新高水位；预留与写入序列有明确失败返回。
// 签名提示：RepositoryResult CsvMediaRepository::updateSequence(const QList<MediaRecord>& records, bool reserve, QString* id) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
RepositoryResult CsvMediaRepository::updateSequence(const QList<MediaRecord>& records, bool reserve, QString* id) {
    const QString statePath = path_ + QStringLiteral(".id-sequence");
    QLockFile lock(statePath + QStringLiteral(".lock"));
    if (!lock.tryLock(1000)) return fail(QStringLiteral("编号状态正被其他进程使用"));
    quint64 maximum = 0;
    const QRegularExpression pattern(QStringLiteral("^M([0-9]{6,})$"));
    // >>> MAIN-DEFENSE | updateSequence｜处理阶段 01
    // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
    // <<< MAIN-DEFENSE
    for (const auto& record : records) {
        const auto match = pattern.match(record.id);
        if (!match.hasMatch()) continue; // 现有 UUID 和其他旧 ID 原样保留。
        bool ok = false;
        const quint64 value = match.captured(1).toULongLong(&ok);
        // >>> MAIN-DEFENSE | updateSequence｜处理阶段 02
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!ok) return fail(QStringLiteral("已有编号超出支持范围"));
        maximum = qMax(maximum, value);
    }
    QFile current(statePath);
    const bool exists = current.exists();
    quint64 previous = 0;
    // >>> MAIN-DEFENSE | updateSequence｜处理阶段 03
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (exists) {
        if (!current.open(QIODevice::ReadOnly)) return fail(QStringLiteral("无法读取编号状态：") + current.errorString());
        const QByteArray bytes = current.readAll();
        if (current.error() != QFileDevice::NoError) return fail(QStringLiteral("读取编号状态失败"));
        const QString text = QString::fromLatin1(bytes).trimmed();
        // >>> MAIN-DEFENSE | updateSequence｜处理阶段 04
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!parseNumber(text, previous)) return fail(QStringLiteral("编号状态损坏，拒绝分配编号"));
        current.close();
    }
    maximum = qMax(maximum, previous);
    // >>> MAIN-DEFENSE | updateSequence｜处理阶段 05
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (reserve) {
        if (maximum == std::numeric_limits<quint64>::max()) return fail(QStringLiteral("编号已用尽"));
        ++maximum;
    }
    // >>> MAIN-DEFENSE | updateSequence｜处理阶段 06
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!exists || maximum != previous) {
        QSaveFile state(statePath);
        if (!state.open(QIODevice::WriteOnly)) return fail(QStringLiteral("无法保存编号状态：") + state.errorString());
        const QByteArray bytes = QByteArray::number(maximum) + '\n';
        // >>> MAIN-DEFENSE | updateSequence｜处理阶段 07
        // 提交这一批修改；必须检查提交结果，调用了commit不等于已经成功。
        // <<< MAIN-DEFENSE
        if (state.write(bytes) != bytes.size() || !state.commit()) return fail(QStringLiteral("保存编号状态失败：") + state.errorString());
    }
    if (reserve && id) *id = QStringLiteral("M%1").arg(maximum, 6, 10, QLatin1Char('0'));
    return {true, {}};
}

// >>> MAIN-DEFENSE | 函数入口｜CsvMediaRepository::load
// 从仓储读取并验证数据；上层在全部加载成功后才采用新快照，避免半份结果。
// 签名提示：RepositoryResult CsvMediaRepository::load(QList<MediaRecord>& records) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
RepositoryResult CsvMediaRepository::load(QList<MediaRecord>& records) {
    QFile file(path_);
    if (!file.open(QIODevice::ReadOnly)) return fail(QStringLiteral("无法打开 CSV：") + file.errorString());
    QByteArray bytes = file.readAll();
    // >>> MAIN-DEFENSE | load｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (file.error() != QFileDevice::NoError) return fail(QStringLiteral("读取 CSV 失败：") + file.errorString());
    if (bytes.startsWith("\xEF\xBB\xBF")) bytes.remove(0, 3);
    const QString text = QString::fromUtf8(bytes);
    if (text.toUtf8() != bytes) return fail(QStringLiteral("CSV 不是有效的 UTF-8"));
    QList<QStringList> rows;
    // >>> MAIN-DEFENSE | load｜处理阶段 02
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!parseCsv(text, rows) || rows.isEmpty()) return fail(QStringLiteral("CSV 格式错误"));
    const auto columns = rows.takeFirst();
    const bool legacy = columns == header.mid(0, 9);
    if (!legacy && columns != header) return fail(QStringLiteral("CSV 表头不匹配"));
    // >>> MAIN-DEFENSE | load｜处理阶段 03
    // 准备本步骤的内存查找、去重或汇总容器；它们不是数据库表，生命周期通常只在本次调用内。
    // <<< MAIN-DEFENSE
    QList<MediaRecord> next; QSet<QString> ids;
    for (const auto& row : rows) {
        if (row.size() != columns.size()) return fail(QStringLiteral("CSV 列数不匹配"));
        MediaRecord r; r.id=row[0]; r.fileName=row[1]; r.fileType=row[2]; r.director=row[4];
        // >>> MAIN-DEFENSE | load｜处理阶段 04
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!legacy) r.category=row[9];
        if (!parseNumber(row[3], r.fileSize) || !parseNumber(row[6], r.viewCount) ||
            !parseList(row[5], r.actors) || !parseList(row[8], r.awards) ||
            !parseMediaRelease(row[7], r) || !validRecord(r) || ids.contains(r.id))
            // >>> MAIN-DEFENSE | load｜处理阶段 05
            // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
            // <<< MAIN-DEFENSE
            return fail(QStringLiteral("CSV 包含无效记录或重复 ID：") + r.id);
        ids.insert(r.id); next.append(r);
    }
    records = std::move(next); return {true, {}};
}

// >>> MAIN-DEFENSE | 函数入口｜CsvMediaRepository::save
// 序列化媒体列表并保存；QSaveFile先写临时文件，commit成功才替换目标。
// 签名提示：RepositoryResult CsvMediaRepository::save(const QList<MediaRecord>& records) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
RepositoryResult CsvMediaRepository::save(const QList<MediaRecord>& records) {
    QSaveFile file(path_);
    if (!file.open(QIODevice::WriteOnly)) return fail(QStringLiteral("无法保存 CSV：") + file.errorString());
    QByteArray output = header.join(',').toUtf8() + '\n';
    // >>> MAIN-DEFENSE | save｜处理阶段 01
    // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
    // <<< MAIN-DEFENSE
    for (const auto& r : records) {
        QStringList values{r.id, r.fileName, r.fileType, QString::number(r.fileSize), r.director,
                           listJson(r.actors), QString::number(r.viewCount), mediaReleaseText(r),
                           listJson(r.awards), r.category};
        // >>> MAIN-DEFENSE | save｜处理阶段 02
        // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
        // <<< MAIN-DEFENSE
        for (auto& value : values) value = quoted(value);
        output += values.join(',').toUtf8() + '\n';
    }
    if (file.write(output) != output.size()) { file.cancelWriting(); return fail(QStringLiteral("写入 CSV 失败：") + file.errorString()); }
    // >>> MAIN-DEFENSE | save｜处理阶段 03
    // 提交这一批修改；必须检查提交结果，调用了commit不等于已经成功。
    // <<< MAIN-DEFENSE
    if (!file.commit()) return fail(QStringLiteral("替换 CSV 失败：") + file.errorString());
    return {true, {}};
}

// >>> MAIN-DEFENSE | 函数入口｜CsvMediaRepository::add
// 在现有记录中添加已校验记录；上层addNew负责分配ID。
// 签名提示：RepositoryResult CsvMediaRepository::add(const MediaRecord& record) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
RepositoryResult CsvMediaRepository::add(const MediaRecord& record) {
    if (!supportedFileTypes().contains(record.fileType))
        return fail(QStringLiteral("不支持的文件类型，请选择支持的音视频扩展名"));
    if (!validRecord(record)) return fail(QStringLiteral("记录字段无效"));
    QList<MediaRecord> all; auto result = load(all); if (!result.success) return result;
    // >>> MAIN-DEFENSE | add｜处理阶段 01
    // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
    // <<< MAIN-DEFENSE
    for (const auto& r : all) if (r.id == record.id) return fail(QStringLiteral("ID 已存在"));
    all.append(record); return save(all);
}

// >>> MAIN-DEFENSE | 函数入口｜CsvMediaRepository::remove
// 按ID删除媒体；业务路径先持久化，再同步内存和检索索引。
// 签名提示：RepositoryResult CsvMediaRepository::remove(const QString& id) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
RepositoryResult CsvMediaRepository::remove(const QString& id) {
    QList<MediaRecord> all; auto result = load(all); if (!result.success) return result;
    for (qsizetype i=0; i<all.size(); ++i) if (all[i].id == id) { all.removeAt(i); return save(all); }
    return fail(QStringLiteral("未找到该 ID"));
}
