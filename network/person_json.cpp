// >>> MAIN-DEFENSE | 主逻辑文件导读
// 人物/资源/资讯及分页JSON转换：64位编号和时间使用十进制字符串；严格检查类型、范围和分页字段，防止协议错位。
// 阅读时先看函数入口说明，再按“处理阶段”往下；到每个return核对成功、空结果和错误是否区别。
// <<< MAIN-DEFENSE
#include "person_json.h"
#include "media_json.h"

#include <QJsonArray>
#include <utility>

namespace person_json {
using namespace care_contract;
namespace {

// >>> MAIN-DEFENSE | 函数入口｜decimal
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：bool decimal(const QJsonValue& value, qint64& result)  
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool decimal(const QJsonValue& value, qint64& result)
{
    if (!value.isString())
        return false;
    const auto text = value.toString();
    // >>> MAIN-DEFENSE | decimal｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (text.isEmpty() || (text.size() > 1 && text.startsWith(QLatin1Char('0'))))
        return false;
    for (const auto character : text)
        if (character < QLatin1Char('0') || character > QLatin1Char('9'))
            // >>> MAIN-DEFENSE | decimal｜处理阶段 02
            // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
            // <<< MAIN-DEFENSE
            return false;
    bool ok = false;
    result = text.toLongLong(&ok);
    return ok;
}

// >>> MAIN-DEFENSE | 函数入口｜optionalTime
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：bool optionalTime(const QJsonValue& value, std::optional<qint64>& result)  
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool optionalTime(const QJsonValue& value, std::optional<qint64>& result)
{
    if (value.isUndefined() || value.isNull()) {
        result.reset();
        // >>> MAIN-DEFENSE | optionalTime｜处理阶段 01
        // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
        // <<< MAIN-DEFENSE
        return true;
    }
    qint64 time = 0;
    if (!decimal(value, time))
        // >>> MAIN-DEFENSE | optionalTime｜处理阶段 02
        // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
        // <<< MAIN-DEFENSE
        return false;
    result = time;
    return true;
}

// >>> MAIN-DEFENSE | 函数入口｜roleName
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：QString roleName(PersonRole role)  
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
QString roleName(PersonRole role)
{
    switch (role) {
    case PersonRole::Actor: return QStringLiteral("actor");
    case PersonRole::Director: return QStringLiteral("director");
    case PersonRole::Musician: return QStringLiteral("musician");
    }
    // >>> MAIN-DEFENSE | roleName｜处理阶段 01
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return {};
}

// >>> MAIN-DEFENSE | 函数入口｜encodeItem
// 编码单个人物、作品或资讯，64位字段按契约表示。
// 签名提示：QJsonObject encodeItem(const PersonSummary& person)  
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
QJsonObject encodeItem(const PersonSummary& person)
{
    QJsonArray roles;
    for (const auto role : person.roles)
        roles.append(roleName(role));
    // >>> MAIN-DEFENSE | encodeItem｜处理阶段 01
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return {{"person_id", person.personId}, {"name", person.name}, {"roles", roles},
            {"followed", person.followed}};
}

// >>> MAIN-DEFENSE | 函数入口｜encodeItem
// 编码单个人物、作品或资讯，64位字段按契约表示。
// 签名提示：QJsonObject encodeItem(const ResourceSummary& work)  
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
QJsonObject encodeItem(const ResourceSummary& work)
{
    const auto& source = work.resource.source;
    const QJsonObject location{{"warehouse_id", QString::number(source.warehouseId)},
                               {"item_id", QString::number(source.itemId)}};
    const QJsonObject resource{{"work_id", work.resource.workId}, {"source", location}};
    QJsonArray people;
    // >>> MAIN-DEFENSE | encodeItem｜处理阶段 01
    // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
    // <<< MAIN-DEFENSE
    for (const auto& person : work.people)
        people.append(encodeItem(person));
    return {{"resource", resource}, {"media", media_json::toJson(work.media)}, {"people", people}};
}

// >>> MAIN-DEFENSE | 函数入口｜encodeItem
// 编码单个人物、作品或资讯，64位字段按契约表示。
// 签名提示：QJsonObject encodeItem(const PersonUpdate& update)  
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
QJsonObject encodeItem(const PersonUpdate& update)
{
    QJsonObject result{{"update_id", update.updateId}, {"person_id", update.personId},
                       {"title", update.title}, {"kind", update.kind == UpdateKind::Release ? "release" : "news"},
                       {"source_url", update.sourceUrl.toString(QUrl::FullyEncoded)},
                       {"fetched_at_utc", QString::number(update.fetchedAtUtc)}, {"stale", update.stale}};
    // >>> MAIN-DEFENSE | encodeItem｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (update.publishedAtUtc)
        result.insert("published_at_utc", QString::number(*update.publishedAtUtc));
    if (update.eventAtUtc)
        result.insert("event_at_utc", QString::number(*update.eventAtUtc));
    // >>> MAIN-DEFENSE | encodeItem｜处理阶段 02
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return result;
}

// >>> MAIN-DEFENSE | 函数入口｜decodeItem
// 验证单条网络对象的身份、角色、来源和时间，不能仅因JSON语法正确就接受。
// 签名提示：bool decodeItem(const QJsonValue& value, PersonSummary& person)  
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool decodeItem(const QJsonValue& value, PersonSummary& person)
{
    const auto object = value.toObject();
    const auto roles = object.value("roles");
    // >>> MAIN-DEFENSE | decodeItem｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!value.isObject() || !object.value("person_id").isString() || !object.value("name").isString() ||
        !roles.isArray() || !object.value("followed").isBool())
        return false;
    PersonSummary parsed{object.value("person_id").toString(), object.value("name").toString(),
                         {}, object.value("followed").toBool()};
    // >>> MAIN-DEFENSE | decodeItem｜处理阶段 02
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!validatePersonId(parsed.personId).ok() || parsed.name.trimmed().isEmpty())
        return false;
    for (const auto& role : roles.toArray()) {
        if (role == "actor") parsed.roles.append(PersonRole::Actor);
        else if (role == "director") parsed.roles.append(PersonRole::Director);
        else if (role == "musician") parsed.roles.append(PersonRole::Musician);
        else return false;
    }
    person = std::move(parsed);
    // >>> MAIN-DEFENSE | decodeItem｜处理阶段 03
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return true;
}

// >>> MAIN-DEFENSE | 函数入口｜decodeItem
// 验证单条网络对象的身份、角色、来源和时间，不能仅因JSON语法正确就接受。
// 签名提示：bool decodeItem(const QJsonValue& value, ResourceSummary& work)  
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool decodeItem(const QJsonValue& value, ResourceSummary& work)
{
    const auto object = value.toObject();
    const auto resource = object.value("resource").toObject();
    const auto source = resource.value("source").toObject();
    const auto media = object.value("media");
    const auto people = object.value("people");
    ResourceSummary parsed;
    parsed.resource.workId = resource.value("work_id").toString();
    QString mediaError;
    // >>> MAIN-DEFENSE | decodeItem｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!value.isObject() || !resource.value("work_id").isString() ||
        !decimal(source.value("warehouse_id"), parsed.resource.source.warehouseId) ||
        !decimal(source.value("item_id"), parsed.resource.source.itemId) ||
        !validate(parsed.resource).ok() || !media.isObject() ||
        !media_json::fromJson(media.toObject(), parsed.media, mediaError) || !people.isArray())
        // >>> MAIN-DEFENSE | decodeItem｜处理阶段 02
        // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
        // <<< MAIN-DEFENSE
        return false;
    for (const auto& entry : people.toArray()) {
        PersonSummary person;
        if (!decodeItem(entry, person))
            // >>> MAIN-DEFENSE | decodeItem｜处理阶段 03
            // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
            // <<< MAIN-DEFENSE
            return false;
        parsed.people.append(std::move(person));
    }
    work = std::move(parsed);
    // >>> MAIN-DEFENSE | decodeItem｜处理阶段 04
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return true;
}

// >>> MAIN-DEFENSE | 函数入口｜decodeItem
// 验证单条网络对象的身份、角色、来源和时间，不能仅因JSON语法正确就接受。
// 签名提示：bool decodeItem(const QJsonValue& value, PersonUpdate& update)  
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool decodeItem(const QJsonValue& value, PersonUpdate& update)
{
    const auto object = value.toObject();
    if (!value.isObject() || !object.value("update_id").isString() || !object.value("person_id").isString() ||
        !object.value("title").isString() || !object.value("source_url").isString() ||
        !object.value("stale").isBool())
        // >>> MAIN-DEFENSE | decodeItem｜处理阶段 01
        // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
        // <<< MAIN-DEFENSE
        return false;
    PersonUpdate parsed;
    parsed.updateId = object.value("update_id").toString();
    parsed.personId = object.value("person_id").toString();
    parsed.title = object.value("title").toString();
    parsed.sourceUrl = QUrl(object.value("source_url").toString(), QUrl::StrictMode);
    parsed.stale = object.value("stale").toBool();
    const auto kind = object.value("kind");
    // >>> MAIN-DEFENSE | decodeItem｜处理阶段 02
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (kind == "news") parsed.kind = UpdateKind::News;
    else if (kind == "release") parsed.kind = UpdateKind::Release;
    else return false;
    if (!optionalTime(object.value("published_at_utc"), parsed.publishedAtUtc) ||
        !optionalTime(object.value("event_at_utc"), parsed.eventAtUtc) ||
        !decimal(object.value("fetched_at_utc"), parsed.fetchedAtUtc) || !validate(parsed).ok())
        // >>> MAIN-DEFENSE | decodeItem｜处理阶段 03
        // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
        // <<< MAIN-DEFENSE
        return false;
    update = std::move(parsed);
    return true;
}

template<class Item>
// >>> MAIN-DEFENSE | 函数入口｜encodePage
// 把列表和下一页位置转换为统一分页JSON。
// 签名提示：QJsonObject encodePage(const Page<Item>& page)  
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
QJsonObject encodePage(const Page<Item>& page)
{
    QJsonArray items;
    for (const auto& item : page.items)
        items.append(encodeItem(item));
    QJsonObject result{{"items", items}};
    // >>> MAIN-DEFENSE | encodePage｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (page.nextOffset)
        result.insert("next_offset", QString::number(*page.nextOffset));
    return result;
}

template<class Item>
// >>> MAIN-DEFENSE | 函数入口｜decodePage
// 检查分页响应结构、数量与下一页位置，再转换业务列表。
// 签名提示：bool decodePage(const QJsonValue& value, const PageRequest& request, Page<Item>& result)  
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool decodePage(const QJsonValue& value, const PageRequest& request, Page<Item>& result)
{
    const auto object = value.toObject();
    const auto items = object.value("items");
    // >>> MAIN-DEFENSE | decodePage｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!value.isObject() || !items.isArray() || !validate(request).ok() || items.toArray().size() > request.limit)
        return false;
    Page<Item> parsed;
    const auto next = object.value("next_offset");
    // >>> MAIN-DEFENSE | decodePage｜处理阶段 02
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!next.isUndefined() && !next.isNull()) {
        qint64 offset = 0;
        if (!decimal(next, offset) || offset <= request.offset || !validate(PageRequest{offset, request.limit}).ok())
            return false;
        parsed.nextOffset = offset;
    }
    // >>> MAIN-DEFENSE | decodePage｜处理阶段 03
    // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
    // <<< MAIN-DEFENSE
    for (const auto& entry : items.toArray()) {
        Item item;
        if (!decodeItem(entry, item))
            return false;
        parsed.items.append(std::move(item));
    }
    result = std::move(parsed);
    // >>> MAIN-DEFENSE | decodePage｜处理阶段 04
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return true;
}
} // namespace

// >>> MAIN-DEFENSE | 函数入口｜encodePageRequest
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：QJsonObject encodePageRequest(const PageRequest& page)  
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
QJsonObject encodePageRequest(const PageRequest& page)
{
    return {{"offset", QString::number(page.offset)}, {"limit", page.limit}};
}

// >>> MAIN-DEFENSE | 函数入口｜decodePageRequest
// 解析平铺offset和limit，offset为十进制字符串，不能混用不一致请求形状。
// 签名提示：bool decodePageRequest(const QJsonObject& payload, PageRequest& page)  
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool decodePageRequest(const QJsonObject& payload, PageRequest& page)
{
    PageRequest parsed;
    parsed.limit = payload.value("limit").toInt(0);
    // >>> MAIN-DEFENSE | decodePageRequest｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!decimal(payload.value("offset"), parsed.offset) || !validate(parsed).ok())
        return false;
    page = parsed;
    return true;
}

// >>> MAIN-DEFENSE | 函数入口｜decodeTimeRange
// 解析时间区间，开始结束按共享契约验证，区间使用左闭右开。
// 签名提示：bool decodeTimeRange(const QJsonValue& value, TimeRange& range)  
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool decodeTimeRange(const QJsonValue& value, TimeRange& range)
{
    const auto object = value.toObject();
    TimeRange parsed;
    // >>> MAIN-DEFENSE | decodeTimeRange｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!decimal(object.value("begin_utc"), parsed.beginUtc) ||
        !decimal(object.value("end_utc"), parsed.endUtc) || !validate(parsed).ok())
        return false;
    range = parsed;
    // >>> MAIN-DEFENSE | decodeTimeRange｜处理阶段 02
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return true;
}

// >>> MAIN-DEFENSE | 函数入口｜encode
// 将已校验业务对象编码为JSON，保留稳定标识和缺失字段语义。
// 签名提示：QJsonObject encode(const Page<PersonSummary>& page) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
QJsonObject encode(const Page<PersonSummary>& page) { return encodePage(page); }
// >>> MAIN-DEFENSE | 函数入口｜encode
// 将已校验业务对象编码为JSON，保留稳定标识和缺失字段语义。
// 签名提示：QJsonObject encode(const Page<ResourceSummary>& page) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
QJsonObject encode(const Page<ResourceSummary>& page) { return encodePage(page); }
// >>> MAIN-DEFENSE | 函数入口｜encode
// 将已校验业务对象编码为JSON，保留稳定标识和缺失字段语义。
// 签名提示：QJsonObject encode(const Page<PersonUpdate>& page) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
QJsonObject encode(const Page<PersonUpdate>& page) { return encodePage(page); }

// >>> MAIN-DEFENSE | 函数入口｜decode
// 将JSON解码并校验字段类型及来源，失败返回空或错误，不擅自补造身份。
// 签名提示：bool decode(const QJsonValue& work, ResourceSummary& result) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool decode(const QJsonValue& work, ResourceSummary& result) { return decodeItem(work, result); }

// >>> MAIN-DEFENSE | 函数入口｜decode
// 将JSON解码并校验字段类型及来源，失败返回空或错误，不擅自补造身份。
// 签名提示：bool decode(const QJsonValue& page, const PageRequest& request, Page<PersonSummary>& result)  
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool decode(const QJsonValue& page, const PageRequest& request, Page<PersonSummary>& result)
{
    return decodePage(page, request, result);
}

// >>> MAIN-DEFENSE | 函数入口｜decode
// 将JSON解码并校验字段类型及来源，失败返回空或错误，不擅自补造身份。
// 签名提示：bool decode(const QJsonValue& page, const PageRequest& request, Page<ResourceSummary>& result)  
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool decode(const QJsonValue& page, const PageRequest& request, Page<ResourceSummary>& result)
{
    return decodePage(page, request, result);
}

// >>> MAIN-DEFENSE | 函数入口｜decode
// 将JSON解码并校验字段类型及来源，失败返回空或错误，不擅自补造身份。
// 签名提示：bool decode(const QJsonValue& page, const PageRequest& request, Page<PersonUpdate>& result)  
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool decode(const QJsonValue& page, const PageRequest& request, Page<PersonUpdate>& result)
{
    return decodePage(page, request, result);
}

} // namespace person_json
