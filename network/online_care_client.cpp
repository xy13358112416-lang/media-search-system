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
void OnlineCareClient::searchPeople(const PersonSearchRequest&, Callback<Page<PersonSummary>> done) { unavailableResult(std::move(done)); }
void OnlineCareClient::listWorkFeedback(const PageRequest&, Callback<Page<WorkFeedback>> done) { unavailableResult(std::move(done)); }
void OnlineCareClient::setWorkFeedback(const WorkFeedbackRequest&, StatusCallback done) { done(unavailable()); }
void OnlineCareClient::listFollows(const PageRequest&, Callback<Page<PersonSummary>> done) { unavailableResult(std::move(done)); }
void OnlineCareClient::setFollow(const QString&, bool, StatusCallback done) { done(unavailable()); }
void OnlineCareClient::getPersonWorks(const QString&, const PageRequest&, Callback<Page<ResourceSummary>> done) { unavailableResult(std::move(done)); }
void OnlineCareClient::getPersonUpdates(const PersonUpdatesRequest&, Callback<Page<PersonUpdate>> done) { unavailableResult(std::move(done)); }
void OnlineCareClient::getTrending(const TrendingRequest&, Callback<TrendingResult> done) { unavailableResult(std::move(done)); }
void OnlineCareClient::getFootprint(const FootprintRequest&, Callback<FootprintReport> done) { unavailableResult(std::move(done)); }
warehouse_contract::RecommendationHandle OnlineCareClient::recommendTemporal(const TemporalRecommendationRequest&, Callback<TemporalRecommendationResult> done) { unavailableResult(std::move(done)); return {}; }
bool OnlineCareClient::cancelRecommendation(const warehouse_contract::RecommendationHandle& handle) { return WarehouseClient::cancelRecommendation(handle); }
