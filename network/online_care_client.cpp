#include "online_care_client.h"
#include "media_json.h"
#include <QJsonArray>
#include <QRegularExpression>

using namespace care_contract;
using warehouse_contract::ErrorCode;
namespace {
Status unavailable() { return {ErrorCode::ServerError, QStringLiteral("此关怀功能尚未接入服务器。")}; }
Status malformed() { return {ErrorCode::ProtocolError, QStringLiteral("服务器关怀响应格式无效。")}; }
template<class T> void unavailableResult(Callback<T> done) { done({unavailable(), {}}); }
QJsonObject sourceJson(const WarehouseItemRef& source) {
    return {{"warehouse_id", QString::number(source.warehouseId)}, {"item_id", QString::number(source.itemId)}};
}
bool positiveId(const QJsonValue& value, qint64& result) {
    if (!value.isString()) return false;
    const QString s = value.toString();
    static const QRegularExpression format(QStringLiteral("\\A[1-9][0-9]*\\z"));
    bool ok = false;
    result = s.toLongLong(&ok);
    return ok && result > 0 && format.match(s).hasMatch();
}
bool nonnegative(const QJsonValue& value, qint64& result) {
    if (!value.isString()) return false;
    static const QRegularExpression format(QStringLiteral("\\A(?:0|[1-9][0-9]*)\\z"));
    const auto s = value.toString(); bool ok = false; result = s.toLongLong(&ok);
    return ok && format.match(s).hasMatch();
}
bool parseRange(const QJsonValue& value, TimeRange& result) {
    if (!value.isObject()) return false;
    const auto o = value.toObject();
    return nonnegative(o.value("begin_utc"), result.beginUtc) && nonnegative(o.value("end_utc"), result.endUtc) && validate(result).ok();
}
bool parsePeriod(const QJsonValue& value, PeriodFootprint& result) {
    if (!value.isObject()) return false;
    const auto o = value.toObject();
    if (!parseRange(o.value("range"), result.range) || !nonnegative(o.value("views"), result.views) ||
        !nonnegative(o.value("searches"), result.searches) || !nonnegative(o.value("unique_works"), result.uniqueWorks) ||
        !nonnegative(o.value("active_days"), result.activeDays) || !o.value("categories").isArray() ||
        !o.value("people").isArray() || !o.value("days").isArray()) return false;
    if (o.value("categories").toArray().size() > 10000 || o.value("people").toArray().size() > 10000 ||
        o.value("days").toArray().size() > 3660) return false;
    const auto coverage = o.value("coverage_start_utc");
    if (!coverage.isUndefined() && !coverage.isNull()) {
        qint64 parsed = 0; if (!nonnegative(coverage, parsed) || parsed >= MaxUtcExclusive) return false;
        result.coverageStartUtc = parsed;
    }
    for (const auto& value : o.value("categories").toArray()) {
        const auto item = value.toObject(); CountBucket bucket;
        if (!value.isObject() || !item.value("key").isString() || !item.value("label").isString() || !nonnegative(item.value("count"), bucket.count)) return false;
        bucket.key = item.value("key").toString(); bucket.label = item.value("label").toString(); result.categories.append(bucket);
    }
    for (const auto& value : o.value("people").toArray()) {
        const auto item = value.toObject(); PersonCount person;
        if (!value.isObject() || !item.value("person_id").isString() || !item.value("name").isString() || !item.value("role").isString() || !nonnegative(item.value("count"), person.count)) return false;
        person.personId = item.value("person_id").toString(); person.name = item.value("name").toString();
        const auto role = item.value("role").toString();
        if (role == "actor") person.role = PersonRole::Actor;
        else if (role == "director") person.role = PersonRole::Director;
        else if (role == "musician") person.role = PersonRole::Musician;
        else return false;
        result.people.append(person);
    }
    for (const auto& value : o.value("days").toArray()) {
        const auto item = value.toObject(); DailyActivity day;
        if (!value.isObject() || !item.value("local_date").isString() || !nonnegative(item.value("views"), day.views) || !nonnegative(item.value("searches"), day.searches)) return false;
        day.localDate = item.value("local_date").toString(); result.days.append(day);
    }
    return true;
}
QJsonObject rangeJson(const TimeRange& range) {
    return {{"begin_utc", QString::number(range.beginUtc)}, {"end_utc", QString::number(range.endUtc)}};
}
bool sameRange(const TimeRange& a, const TimeRange& b) { return a.beginUtc == b.beginUtc && a.endUtc == b.endUtc; }
bool parsePerson(const QJsonValue& value, PersonSummary& person);
bool parseWork(const QJsonValue& value, ResourceSummary& result) {
    const auto work = value.toObject(); const auto refValue = work.value("resource"); const auto ref = refValue.toObject();
    const auto sourceValue = ref.value("source"); const auto source = sourceValue.toObject(); QString error;
    if (!value.isObject() || !refValue.isObject() || !sourceValue.isObject() || !ref.value("work_id").isString() ||
        !positiveId(source.value("warehouse_id"), result.resource.source.warehouseId) ||
        !positiveId(source.value("item_id"), result.resource.source.itemId) || !work.value("media").isObject() ||
        !media_json::fromJson(work.value("media").toObject(), result.media, error) || !work.value("people").isArray()) return false;
    result.resource.workId = ref.value("work_id").toString();
    if (!validate(result.resource).ok()) return false;
    for (const auto& entry : work.value("people").toArray()) {
        PersonSummary person;
        if (!parsePerson(entry, person)) return false;
        result.people.append(person);
    }
    return true;
}
QJsonObject preferenceJson(const recommendation_contract::PreferenceSelection& value) {
    using recommendation_contract::PreferenceMode;
    const QString mode = value.mode == PreferenceMode::Auto ? "auto" : value.mode == PreferenceMode::Any ? "any" : "explicit";
    return {{"mode", mode}, {"value", value.value}};
}
bool parsePerson(const QJsonValue& value, PersonSummary& person) {
    const auto p = value.toObject();
    if (!value.isObject() || !p.value("person_id").isString() || p.value("person_id").toString().isEmpty() ||
        !p.value("name").isString() || !p.value("roles").isArray() || !p.value("followed").isBool()) return false;
    person.personId = p.value("person_id").toString(); person.name = p.value("name").toString(); person.followed = p.value("followed").toBool();
    for (const auto& role : p.value("roles").toArray()) {
        if (role == "actor") person.roles.append(PersonRole::Actor);
        else if (role == "director") person.roles.append(PersonRole::Director);
        else if (role == "musician") person.roles.append(PersonRole::Musician);
        else return false;
    }
    return validate(PersonUpdatesRequest{person.personId, {0, 1}, {0, 1}}).ok() && !person.name.trimmed().isEmpty();
}
template<class T, class Parser> bool parsePage(const QJsonValue& value, const PageRequest& request, Page<T>& page, Parser parser) {
    const auto object = value.toObject();
    if (!value.isObject() || !object.value("items").isArray() || object.value("items").toArray().size() > request.limit) return false;
    for (const auto& entry : object.value("items").toArray()) { T item; if (!parser(entry, item)) return false; page.items.append(item); }
    const auto next = object.value("next_offset");
    if (!next.isUndefined() && !next.isNull()) {
        qint64 offset = 0;
        if (!nonnegative(next, offset) || offset <= request.offset || !validate(PageRequest{offset, request.limit}).ok()) return false;
        page.nextOffset = offset;
    }
    return true;
}
}

void OnlineCareClient::recordBehavior(const BehaviorEvent& event, StatusCallback done) {
    const auto status = validate(event);
    if (!status.ok()) { done(status); return; }
    QJsonObject json{{"event_id", event.eventId}, {"action", event.action == BehaviorAction::View ? "view" : "search"}, {"query", event.query}};
    if (event.resource) json.insert("resource", QJsonObject{{"work_id", event.resource->workId}, {"source", sourceJson(event.resource->source)}});
    request("record_behavior", {{"event", json}}, [done=std::move(done)](Status result, QJsonObject) { done(result); });
}
void OnlineCareClient::getParticipationSettings(Callback<ParticipationSettings> done) {
    request("get_participation_settings", {}, [this, done=std::move(done)](Status status, QJsonObject payload) {
        if (!status.ok()) { done({status, {}}); return; }
        const auto value = payload.value("settings");
        const auto o = value.toObject();
        if (!value.isObject() || !o.value("history_enabled").isBool() || !o.value("personalization_enabled").isBool() ||
            !o.value("contribute_public_trends").isBool()) { abortProtocol(); done({malformed(), {}}); return; }
        done({{}, ParticipationSettings{o.value("history_enabled").toBool(), o.value("personalization_enabled").toBool(),
                                        o.value("contribute_public_trends").toBool()}});
    });
}
void OnlineCareClient::setParticipationSettings(const ParticipationSettings& settings, StatusCallback done) {
    const QJsonObject o{{"history_enabled", settings.historyEnabled}, {"personalization_enabled", settings.personalizationEnabled},
                        {"contribute_public_trends", settings.contributePublicTrends}};
    request("set_participation_settings", {{"settings", o}}, [done=std::move(done)](Status s, QJsonObject) { done(s); });
}
void OnlineCareClient::clearHistory(StatusCallback done) {
    request("clear_history", {}, [done=std::move(done)](Status s, QJsonObject) { done(s); });
}
void OnlineCareClient::getResource(const WarehouseItemRef& source, Callback<ResourceSummary> done) {
    const auto status = validate(source);
    if (!status.ok()) { done({status, {}}); return; }
    request("get_resource", {{"source", sourceJson(source)}}, [this, source, done=std::move(done)](Status s, QJsonObject payload) {
        if (!s.ok()) { done({s, {}}); return; }
        const auto value = payload.value("work"); const auto work = value.toObject();
        const auto refValue = work.value("resource"); const auto ref = refValue.toObject();
        const auto sourceValue = ref.value("source"); const auto src = sourceValue.toObject();
        ResourceSummary result; QString error;
        if (!value.isObject() || !refValue.isObject() || !ref.value("work_id").isString() || !sourceValue.isObject() ||
            !positiveId(src.value("warehouse_id"), result.resource.source.warehouseId) ||
            !positiveId(src.value("item_id"), result.resource.source.itemId) ||
            result.resource.source.warehouseId != source.warehouseId || result.resource.source.itemId != source.itemId || !work.value("media").isObject() ||
            !media_json::fromJson(work.value("media").toObject(), result.media, error) || !work.value("people").isArray()) {
            abortProtocol(); done({malformed(), {}}); return;
        }
        result.resource.workId = ref.value("work_id").toString();
        if (!validate(result.resource).ok()) { abortProtocol(); done({malformed(), {}}); return; }
        for (const auto& entry : work.value("people").toArray()) {
            if (!entry.isObject()) { abortProtocol(); done({malformed(), {}}); return; }
            const auto p = entry.toObject();
            if (!p.value("person_id").isString() || p.value("person_id").toString().isEmpty() ||
                !p.value("name").isString() || !p.value("roles").isArray() || !p.value("followed").isBool()) {
                abortProtocol(); done({malformed(), {}}); return;
            }
            PersonSummary person{p.value("person_id").toString(), p.value("name").toString(), {}, p.value("followed").toBool()};
            for (const auto& role : p.value("roles").toArray()) {
                if (role == "actor") person.roles.append(PersonRole::Actor);
                else if (role == "director") person.roles.append(PersonRole::Director);
                else if (role == "musician") person.roles.append(PersonRole::Musician);
                else { abortProtocol(); done({malformed(), {}}); return; }
            }
            result.people.append(person);
        }
        done({{}, result});
    });
}
void OnlineCareClient::searchPeople(const PersonSearchRequest& input, Callback<Page<PersonSummary>> done) {
    const auto status = validate(input); if (!status.ok()) { done({status, {}}); return; }
    request("search_people", {{"query", input.query}, {"offset", QString::number(input.page.offset)}, {"limit", input.page.limit}},
        [this, input, done=std::move(done)](Status result, QJsonObject payload) {
        if (!result.ok()) { done({result, {}}); return; }
        Page<PersonSummary> page;
        if (!parsePage(payload.value("page"), input.page, page, parsePerson)) { abortProtocol(); done({malformed(), {}}); return; }
        done({{}, page});
    });
}
void OnlineCareClient::listWorkFeedback(const PageRequest& input, Callback<Page<WorkFeedback>> done) {
    const auto status = validate(input);
    if (!status.ok()) { done({status, {}}); return; }
    request("list_work_feedback", {{"offset", QString::number(input.offset)}, {"limit", input.limit}},
        [this, input, done=std::move(done)](Status result, QJsonObject payload) {
        if (!result.ok()) { done({result, {}}); return; }
        const auto value = payload.value("page"); const auto object = value.toObject(); Page<WorkFeedback> page;
        bool ok = value.isObject() && object.value("items").isArray() && object.value("items").toArray().size() <= input.limit;
        const auto next = object.value("next_offset");
        if (!next.isUndefined() && !next.isNull()) {
            qint64 parsed = 0;
            if (!nonnegative(next, parsed) || parsed != input.offset + input.limit) ok = false;
            else page.nextOffset = parsed;
        }
        for (const auto& entry : object.value("items").toArray()) {
            const auto row = entry.toObject(); WorkFeedback item;
            if (!entry.isObject() || row.value("feedback") != "not_interested" || !parseWork(row.value("work"), item.work)) { ok = false; break; }
            item.feedback = WorkFeedbackKind::NotInterested; page.items.append(item);
        }
        if (!ok) { abortProtocol(); done({malformed(), {}}); return; }
        done({{}, page});
    });
}
void OnlineCareClient::setWorkFeedback(const WorkFeedbackRequest& input, StatusCallback done) {
    const auto status = validate(input);
    if (!status.ok()) { done(status); return; }
    const auto& ref = input.resource;
    const QJsonObject resource{{"work_id", ref.workId}, {"source", sourceJson(ref.source)}};
    const QString feedback = input.feedback == WorkFeedbackKind::Neutral ? "neutral" : "not_interested";
    request("set_work_feedback", {{"resource", resource}, {"feedback", feedback}}, [done=std::move(done)](Status result, QJsonObject) { done(result); });
}
void OnlineCareClient::listFollows(const PageRequest& input, Callback<Page<PersonSummary>> done) {
    const auto status = validate(input); if (!status.ok()) { done({status, {}}); return; }
    request("list_follows", {{"offset", QString::number(input.offset)}, {"limit", input.limit}},
        [this, input, done=std::move(done)](Status result, QJsonObject payload) {
        if (!result.ok()) { done({result, {}}); return; }
        Page<PersonSummary> page;
        if (!parsePage(payload.value("page"), input, page, parsePerson)) { abortProtocol(); done({malformed(), {}}); return; }
        done({{}, page});
    });
}
void OnlineCareClient::setFollow(const QString& personId, bool followed, StatusCallback done) {
    const auto status = validate(PersonUpdatesRequest{personId, {0, 1}, {0, 1}});
    if (!status.ok()) { done(status); return; }
    request("set_follow", {{"person_id", personId}, {"followed", followed}}, [done=std::move(done)](Status result, QJsonObject) { done(result); });
}
void OnlineCareClient::getPersonWorks(const QString& personId, const PageRequest& input, Callback<Page<ResourceSummary>> done) {
    const auto status = validate(PersonUpdatesRequest{personId, {0, 1}, input});
    if (!status.ok()) { done({status, {}}); return; }
    request("get_person_works", {{"person_id", personId}, {"offset", QString::number(input.offset)}, {"limit", input.limit}},
        [this, input, done=std::move(done)](Status result, QJsonObject payload) {
        if (!result.ok()) { done({result, {}}); return; }
        Page<ResourceSummary> page;
        if (!parsePage(payload.value("page"), input, page, parseWork)) { abortProtocol(); done({malformed(), {}}); return; }
        done({{}, page});
    });
}
void OnlineCareClient::getPersonUpdates(const PersonUpdatesRequest&, Callback<Page<PersonUpdate>> done) { unavailableResult(std::move(done)); }
void OnlineCareClient::getTrending(const TrendingRequest& requestValue, Callback<TrendingResult> done) {
    const auto valid = validate(requestValue);
    if (!valid.ok()) { done({valid, {}}); return; }
    const QString kind = requestValue.kind == TrendingKind::Works ? "works" : "search_terms";
    request("get_trending", {{"kind", kind}, {"range", rangeJson(requestValue.range)}, {"limit", requestValue.limit}},
            [this, requestValue, kind, done=std::move(done)](Status status, QJsonObject payload) {
        if (!status.ok()) { done({status, {}}); return; }
        const auto value = payload.value("result"); const auto object = value.toObject();
        TrendingResult result; result.kind = requestValue.kind;
        bool ok = value.isObject() && object.value("kind") == kind && parseRange(object.value("range"), result.range) &&
            sameRange(result.range, requestValue.range) && nonnegative(object.value("generated_at_utc"), result.generatedAtUtc) &&
            result.generatedAtUtc < MaxUtcExclusive && object.value("summary").isString() &&
            object.value("summary_from_model").isBool() && object.value("contains_synthetic_data").isBool() &&
            object.value("items").isArray() && object.value("items").toArray().size() <= requestValue.limit;
        for (const auto& entry : object.value("items").toArray()) {
            if (!entry.isObject()) { ok = false; break; }
            const auto row = entry.toObject(); TrendingItem item;
            if (!row.value("term").isString() || !nonnegative(row.value("distinct_users"), item.distinctUsers) ||
                !nonnegative(row.value("effective_events"), item.effectiveEvents) || item.distinctUsers < 3 ||
                item.effectiveEvents < item.distinctUsers) { ok = false; break; }
            item.term = row.value("term").toString();
            if (requestValue.kind == TrendingKind::Works) {
                const auto workValue = row.value("work"); const auto work = workValue.toObject();
                const auto refValue = work.value("resource"); const auto ref = refValue.toObject();
                const auto srcValue = ref.value("source"); const auto src = srcValue.toObject();
                ResourceSummary resource; QString error;
                if (!item.term.isEmpty() || !workValue.isObject() || !refValue.isObject() || !srcValue.isObject() ||
                    !ref.value("work_id").isString() || !positiveId(src.value("warehouse_id"), resource.resource.source.warehouseId) ||
                    !positiveId(src.value("item_id"), resource.resource.source.itemId) || !work.value("media").isObject() ||
                    !media_json::fromJson(work.value("media").toObject(), resource.media, error) ||
                    !work.value("people").isArray()) { ok = false; break; }
                resource.resource.workId = ref.value("work_id").toString();
                if (!validate(resource.resource).ok()) { ok = false; break; }
                for (const auto& personValue : work.value("people").toArray()) {
                    const auto p = personValue.toObject();
                    if (!personValue.isObject() || !p.value("person_id").isString() || p.value("person_id").toString().isEmpty() ||
                        !p.value("name").isString() || !p.value("roles").isArray() || !p.value("followed").isBool()) { ok = false; break; }
                    PersonSummary person{p.value("person_id").toString(), p.value("name").toString(), {}, p.value("followed").toBool()};
                    for (const auto& role : p.value("roles").toArray()) {
                        if (role == "actor") person.roles.append(PersonRole::Actor);
                        else if (role == "director") person.roles.append(PersonRole::Director);
                        else if (role == "musician") person.roles.append(PersonRole::Musician);
                        else { ok = false; break; }
                    }
                    if (!ok) break;
                    resource.people.append(person);
                }
                if (!ok) break;
                item.work = resource;
            } else if (item.term.isEmpty() || (!row.value("work").isUndefined() && !row.value("work").isNull())) {
                ok = false; break;
            }
            result.items.append(item);
        }
        if (!ok) { abortProtocol(); done({malformed(), {}}); return; }
        result.summary = object.value("summary").toString();
        result.summaryFromModel = object.value("summary_from_model").toBool();
        result.containsSyntheticData = object.value("contains_synthetic_data").toBool();
        done({{}, result});
    });
}
void OnlineCareClient::getFootprint(const FootprintRequest& requestValue, Callback<FootprintReport> done) {
    const auto valid = validate(requestValue);
    if (!valid.ok()) { done({valid, {}}); return; }
    QJsonObject payload{{"range", rangeJson(requestValue.range)}};
    if (requestValue.comparison) payload.insert("comparison", rangeJson(*requestValue.comparison));
    request("get_footprint", payload, [this, requestValue, done=std::move(done)](Status status, QJsonObject response) {
        if (!status.ok()) { done({status, {}}); return; }
        const auto value = response.value("report"); const auto o = value.toObject();
        FootprintReport report;
        bool ok = value.isObject() && parsePeriod(o.value("current"), report.current) && sameRange(report.current.range, requestValue.range) &&
                  nonnegative(o.value("generated_at_utc"), report.generatedAtUtc) && report.generatedAtUtc < MaxUtcExclusive &&
                  o.value("message").isString() && o.value("contains_synthetic_data").isBool();
        const auto comparison = o.value("comparison");
        if (!comparison.isUndefined() && !comparison.isNull()) {
            PeriodFootprint parsed;
            if (!requestValue.comparison || !parsePeriod(comparison, parsed) || !sameRange(parsed.range, *requestValue.comparison)) ok = false;
            else report.comparison = parsed;
        } else if (requestValue.comparison) ok = false;
        if (!ok) { abortProtocol(); done({malformed(), {}}); return; }
        report.message = o.value("message").toString(); report.containsSyntheticData = o.value("contains_synthetic_data").toBool();
        done({{}, report});
    });
}
warehouse_contract::RecommendationHandle OnlineCareClient::recommendTemporal(const TemporalRecommendationRequest& input, Callback<TemporalRecommendationResult> done) {
    const auto valid = validate(input);
    if (!valid.ok()) { done({valid, {}}); return {}; }
    const QString scene = input.scene == BrowseScene::Neutral ? "neutral" : input.scene == BrowseScene::Relax ? "relax" :
                          input.scene == BrowseScene::Focus ? "focus" : "explore";
    const auto id = request("recommend_temporal", {{"category", preferenceJson(input.category)}, {"file_type", preferenceJson(input.fileType)},
        {"scene", scene}, {"limit", input.limit}}, [this, input, done=std::move(done)](Status status, QJsonObject payload) {
        if (!status.ok()) { done({status, {}}); return; }
        const auto value = payload.value("result"); const auto object = value.toObject();
        TemporalRecommendationResult result;
        bool ok = value.isObject() && object.value("items").isArray() && object.value("items").toArray().size() <= input.limit &&
            object.value("message").isString() && object.value("fallback").isBool() && object.value("engine") == "temporal" &&
            object.value("contains_synthetic_data").isBool() && nonnegative(object.value("generated_at_utc"), result.generatedAtUtc) &&
            result.generatedAtUtc < MaxUtcExclusive;
        for (const auto& entry : object.value("items").toArray()) {
            const auto row = entry.toObject(); RecommendedResource item;
            if (!entry.isObject() || !row.value("reason").isString() || !parseWork(row.value("work"), item.work)) { ok = false; break; }
            item.reason = row.value("reason").toString(); result.items.append(item);
        }
        if (!ok) { abortProtocol(); done({malformed(), {}}); return; }
        result.message = object.value("message").toString(); result.fallback = object.value("fallback").toBool();
        result.engine = RecommendationEngine::Temporal; result.containsSyntheticData = object.value("contains_synthetic_data").toBool();
        done({{}, result});
    });
    return id.isEmpty() ? warehouse_contract::RecommendationHandle{} : handlePrefix_ + id;
}
bool OnlineCareClient::cancelRecommendation(const warehouse_contract::RecommendationHandle& handle) { return WarehouseClient::cancelRecommendation(handle); }
