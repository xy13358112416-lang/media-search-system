// >>> DEFENSE-NOTE | 文件导读｜在线扩展服务客户端
// 阅读路线：契约校验 → JSON序列化 → 同一登录连接发送 → 严格响应解析
// 联调关系：Qt页面通过本适配访问服务端；本人身份来自会话，不让调用者随意指定userId。
// 标有“答辩导读”的内容仅为讲解注释；原有条件、接口和算法不变。
// <<< DEFENSE-NOTE | 本段讲解结束
#include "online_care_client.h"
#include "media_json.h"
#include "person_json.h"
#include <QJsonArray>
#include <QRegularExpression>
#include <algorithm>
#include <type_traits>

using namespace care_contract;
using warehouse_contract::ErrorCode;
namespace {
Status malformed() { return {ErrorCode::ProtocolError, QStringLiteral("服务器关怀响应格式无效。")}; }
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
QJsonObject preferenceJson(const recommendation_contract::PreferenceSelection& value) {
    using recommendation_contract::PreferenceMode;
    const QString mode = value.mode == PreferenceMode::Auto ? "auto" : value.mode == PreferenceMode::Any ? "any" : "explicit";
    return {{"mode", mode}, {"value", value.value}};
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
template<class Item>
void OnlineCareClient::requestPersonPage(const QString& type, QJsonObject payload, const PageRequest& page,
                                         Callback<Page<Item>> done)
{
    const auto pagination = person_json::encodePageRequest(page);
    for (auto it = pagination.constBegin(); it != pagination.constEnd(); ++it)
        payload.insert(it.key(), it.value());
    request(type, payload, [this, page, payload, done = std::move(done)](Status status, QJsonObject response) {
        if (!status.ok()) {
            done({status, {}});
            return;
        }
        Page<Item> result;
        if (!person_json::decode(response.value("page"), page, result)) {
            abortProtocol();
            done({malformed(), {}});
            return;
        }
        if constexpr (std::is_same_v<Item, PersonUpdate>) {
            const auto personId = payload.value("person_id").toString();
            const auto wrongPerson = [&personId](const PersonUpdate& update) { return update.personId != personId; };
            if (std::any_of(result.items.cbegin(), result.items.cend(), wrongPerson)) {
                abortProtocol();
                done({malformed(), {}});
                return;
            }
        }
        done({{}, std::move(result)});
    });
}

void OnlineCareClient::searchPeople(const PersonSearchRequest& input, Callback<Page<PersonSummary>> done)
{
    const auto status = validate(input);
    if (!status.ok()) { done({status, {}}); return; }
    requestPersonPage("search_people", {{"query", input.query}}, input.page, std::move(done));
}

void OnlineCareClient::searchPersonWeb(const QString& name, Callback<QList<PersonWebSource>> done)
{
    request("search_person_web", {{"name", name}}, [done = std::move(done)](Status status, QJsonObject payload) {
        if (!status.ok()) {
            done({status, {}});
            return;
        }
        const auto encoded = payload.value("sources");
        if (!encoded.isArray() || encoded.toArray().size() > 3) {
            done({malformed(), {}});
            return;
        }
        QList<PersonWebSource> sources;
        for (const auto& row : encoded.toArray()) {
            const auto item = row.toObject();
            const auto title = item.value("title").toString();
            const QUrl url(item.value("url").toString(), QUrl::StrictMode);
            if (title.isEmpty() || title.size() > 200 || !url.isValid() || url.scheme() != "https" ||
                url.host().isEmpty() || !url.userInfo().isEmpty() || url.port() != -1 || url.toEncoded().size() > 2048) {
                done({malformed(), {}});
                return;
            }
            PersonWebSource source{title, url};
            source.biography = item.value("biography").toString();
            const auto works = item.value("works");
            if (source.biography.size() > 1200 || (!works.isUndefined() && (!works.isArray() || works.toArray().size() > 20))) {
                done({malformed(), {}}); return;
            }
            for (const auto& entry : works.toArray()) {
                const auto row = entry.toObject();
                PersonWebWork work{row.value("title").toString(), row.value("year").toString(),
                                   row.value("role").toString(), QUrl(row.value("source_url").toString(), QUrl::StrictMode)};
                if (work.title.trimmed().isEmpty() || work.title.size() > 200 || work.role.size() > 80 ||
                    (!work.year.isEmpty() && !QRegularExpression("^[0-9]{4}$").match(work.year).hasMatch()) ||
                    !work.sourceUrl.isValid() || work.sourceUrl.scheme() != "https" || work.sourceUrl.host().isEmpty() ||
                    !work.sourceUrl.userInfo().isEmpty() || (work.sourceUrl.port() != -1 && work.sourceUrl.port() != 443)) {
                    done({malformed(), {}}); return;
                }
                source.works.append(work);
            }
            sources.append(source);
        }
        done({{}, std::move(sources)});
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
            if (!entry.isObject() || row.value("feedback") != "not_interested" || !person_json::decode(row.value("work"), item.work)) { ok = false; break; }
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
void OnlineCareClient::listFollows(const PageRequest& input, Callback<Page<PersonSummary>> done)
{
    const auto status = validate(input);
    if (!status.ok()) { done({status, {}}); return; }
    requestPersonPage("list_follows", {}, input, std::move(done));
}
void OnlineCareClient::setFollow(const QString& personId, bool followed, StatusCallback done) {
    const auto status = validatePersonId(personId);
    if (!status.ok()) { done(status); return; }
    request("set_follow", {{"person_id", personId}, {"followed", followed}}, [done=std::move(done)](Status result, QJsonObject) { done(result); });
}
void OnlineCareClient::getPersonWorks(const QString& personId, const PageRequest& input, Callback<Page<ResourceSummary>> done)
{
    const auto person = validatePersonId(personId);
    if (!person.ok()) { done({person, {}}); return; }
    const auto status = validate(input);
    if (!status.ok()) { done({status, {}}); return; }
    requestPersonPage("get_person_works", {{"person_id", personId}}, input, std::move(done));
}

void OnlineCareClient::getPersonUpdates(const PersonUpdatesRequest& input, Callback<Page<PersonUpdate>> done)
{
    const auto status = validate(input);
    if (!status.ok()) { done({status, {}}); return; }
    requestPersonPage("get_person_updates", {{"person_id", input.personId}, {"range", rangeJson(input.range)}},
                      input.page, std::move(done));
}
// >>> DEFENSE-NOTE | getTrending = 热榜JSON适配
// 发送kind/range/limit/summary_mode；只有Model使用较长等待，解析时核对响应范围及计数字段。
// <<< DEFENSE-NOTE | 本段讲解结束
void OnlineCareClient::getTrending(const TrendingRequest& requestValue, Callback<TrendingResult> done) {
    const auto valid = validate(requestValue);
    if (!valid.ok()) { done({valid, {}}); return; }
    const QString kind = requestValue.kind == TrendingKind::Works ? "works" : "search_terms";
    const QString summaryMode = requestValue.summaryMode == TrendingSummaryMode::Model ? "model" : "rules";
    request("get_trending", {{"kind", kind}, {"range", rangeJson(requestValue.range)}, {"limit", requestValue.limit},
                            {"summary_mode", summaryMode}},
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
// >>> DEFENSE-NOTE | getFootprint = 足迹JSON适配
// 校验请求与返回日期范围，解析当前期/比较期及覆盖信息；协议损坏返回ProtocolError。
// <<< DEFENSE-NOTE | 本段讲解结束
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
// >>> DEFENSE-NOTE | recommendTemporal = 时序/场景推荐的客户端请求
// 三态偏好转JSON，scene转neutral/relax/focus/explore；通过同一登录连接发送。
// 返回推荐句柄用于取消；作品顺序保留，不在客户端重新计算分数。
// <<< DEFENSE-NOTE | 本段讲解结束
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
            if (!entry.isObject() || !row.value("reason").isString() || !person_json::decode(row.value("work"), item.work)) { ok = false; break; }
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
