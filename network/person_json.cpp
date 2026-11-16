#include "person_json.h"
#include "media_json.h"

#include <QJsonArray>
#include <utility>

namespace person_json {
using namespace care_contract;
namespace {

bool decimal(const QJsonValue& value, qint64& result)
{
    if (!value.isString())
        return false;
    const auto text = value.toString();
    if (text.isEmpty() || (text.size() > 1 && text.startsWith(QLatin1Char('0'))))
        return false;
    for (const auto character : text)
        if (character < QLatin1Char('0') || character > QLatin1Char('9'))
            return false;
    bool ok = false;
    result = text.toLongLong(&ok);
    return ok;
}

bool optionalTime(const QJsonValue& value, std::optional<qint64>& result)
{
    if (value.isUndefined() || value.isNull()) {
        result.reset();
        return true;
    }
    qint64 time = 0;
    if (!decimal(value, time))
        return false;
    result = time;
    return true;
}

QString roleName(PersonRole role)
{
    switch (role) {
    case PersonRole::Actor: return QStringLiteral("actor");
    case PersonRole::Director: return QStringLiteral("director");
    case PersonRole::Musician: return QStringLiteral("musician");
    }
    return {};
}

QJsonObject encodeItem(const PersonSummary& person)
{
    QJsonArray roles;
    for (const auto role : person.roles)
        roles.append(roleName(role));
    return {{"person_id", person.personId}, {"name", person.name}, {"roles", roles},
            {"followed", person.followed}};
}

QJsonObject encodeItem(const ResourceSummary& work)
{
    const auto& source = work.resource.source;
    const QJsonObject location{{"warehouse_id", QString::number(source.warehouseId)},
                               {"item_id", QString::number(source.itemId)}};
    const QJsonObject resource{{"work_id", work.resource.workId}, {"source", location}};
    QJsonArray people;
    for (const auto& person : work.people)
        people.append(encodeItem(person));
    return {{"resource", resource}, {"media", media_json::toJson(work.media)}, {"people", people}};
}

QJsonObject encodeItem(const PersonUpdate& update)
{
    QJsonObject result{{"update_id", update.updateId}, {"person_id", update.personId},
                       {"title", update.title}, {"kind", update.kind == UpdateKind::Release ? "release" : "news"},
                       {"source_url", update.sourceUrl.toString(QUrl::FullyEncoded)},
                       {"fetched_at_utc", QString::number(update.fetchedAtUtc)}, {"stale", update.stale}};
    if (update.publishedAtUtc)
        result.insert("published_at_utc", QString::number(*update.publishedAtUtc));
    if (update.eventAtUtc)
        result.insert("event_at_utc", QString::number(*update.eventAtUtc));
    return result;
}

bool decodeItem(const QJsonValue& value, PersonSummary& person)
{
    const auto object = value.toObject();
    const auto roles = object.value("roles");
    if (!value.isObject() || !object.value("person_id").isString() || !object.value("name").isString() ||
        !roles.isArray() || !object.value("followed").isBool())
        return false;
    PersonSummary parsed{object.value("person_id").toString(), object.value("name").toString(),
                         {}, object.value("followed").toBool()};
    if (!validatePersonId(parsed.personId).ok() || parsed.name.trimmed().isEmpty())
        return false;
    for (const auto& role : roles.toArray()) {
        if (role == "actor") parsed.roles.append(PersonRole::Actor);
        else if (role == "director") parsed.roles.append(PersonRole::Director);
        else if (role == "musician") parsed.roles.append(PersonRole::Musician);
        else return false;
    }
    person = std::move(parsed);
    return true;
}

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
    if (!value.isObject() || !resource.value("work_id").isString() ||
        !decimal(source.value("warehouse_id"), parsed.resource.source.warehouseId) ||
        !decimal(source.value("item_id"), parsed.resource.source.itemId) ||
        !validate(parsed.resource).ok() || !media.isObject() ||
        !media_json::fromJson(media.toObject(), parsed.media, mediaError) || !people.isArray())
        return false;
    for (const auto& entry : people.toArray()) {
        PersonSummary person;
        if (!decodeItem(entry, person))
            return false;
        parsed.people.append(std::move(person));
    }
    work = std::move(parsed);
    return true;
}

bool decodeItem(const QJsonValue& value, PersonUpdate& update)
{
    const auto object = value.toObject();
    if (!value.isObject() || !object.value("update_id").isString() || !object.value("person_id").isString() ||
        !object.value("title").isString() || !object.value("source_url").isString() ||
        !object.value("stale").isBool())
        return false;
    PersonUpdate parsed;
    parsed.updateId = object.value("update_id").toString();
    parsed.personId = object.value("person_id").toString();
    parsed.title = object.value("title").toString();
    parsed.sourceUrl = QUrl(object.value("source_url").toString(), QUrl::StrictMode);
    parsed.stale = object.value("stale").toBool();
    const auto kind = object.value("kind");
    if (kind == "news") parsed.kind = UpdateKind::News;
    else if (kind == "release") parsed.kind = UpdateKind::Release;
    else return false;
    if (!optionalTime(object.value("published_at_utc"), parsed.publishedAtUtc) ||
        !optionalTime(object.value("event_at_utc"), parsed.eventAtUtc) ||
        !decimal(object.value("fetched_at_utc"), parsed.fetchedAtUtc) || !validate(parsed).ok())
        return false;
    update = std::move(parsed);
    return true;
}

template<class Item>
QJsonObject encodePage(const Page<Item>& page)
{
    QJsonArray items;
    for (const auto& item : page.items)
        items.append(encodeItem(item));
    QJsonObject result{{"items", items}};
    if (page.nextOffset)
        result.insert("next_offset", QString::number(*page.nextOffset));
    return result;
}

template<class Item>
bool decodePage(const QJsonValue& value, const PageRequest& request, Page<Item>& result)
{
    const auto object = value.toObject();
    const auto items = object.value("items");
    if (!value.isObject() || !items.isArray() || !validate(request).ok() || items.toArray().size() > request.limit)
        return false;
    Page<Item> parsed;
    const auto next = object.value("next_offset");
    if (!next.isUndefined() && !next.isNull()) {
        qint64 offset = 0;
        if (!decimal(next, offset) || offset <= request.offset || !validate(PageRequest{offset, request.limit}).ok())
            return false;
        parsed.nextOffset = offset;
    }
    for (const auto& entry : items.toArray()) {
        Item item;
        if (!decodeItem(entry, item))
            return false;
        parsed.items.append(std::move(item));
    }
    result = std::move(parsed);
    return true;
}
} // namespace

QJsonObject encodePageRequest(const PageRequest& page)
{
    return {{"offset", QString::number(page.offset)}, {"limit", page.limit}};
}

bool decodePageRequest(const QJsonObject& payload, PageRequest& page)
{
    PageRequest parsed;
    parsed.limit = payload.value("limit").toInt(0);
    if (!decimal(payload.value("offset"), parsed.offset) || !validate(parsed).ok())
        return false;
    page = parsed;
    return true;
}

QJsonObject encode(const Page<PersonSummary>& page) { return encodePage(page); }
QJsonObject encode(const Page<ResourceSummary>& page) { return encodePage(page); }
QJsonObject encode(const Page<PersonUpdate>& page) { return encodePage(page); }

bool decode(const QJsonValue& work, ResourceSummary& result) { return decodeItem(work, result); }

bool decode(const QJsonValue& page, const PageRequest& request, Page<PersonSummary>& result)
{
    return decodePage(page, request, result);
}

bool decode(const QJsonValue& page, const PageRequest& request, Page<ResourceSummary>& result)
{
    return decodePage(page, request, result);
}

bool decode(const QJsonValue& page, const PageRequest& request, Page<PersonUpdate>& result)
{
    return decodePage(page, request, result);
}

} // namespace person_json
