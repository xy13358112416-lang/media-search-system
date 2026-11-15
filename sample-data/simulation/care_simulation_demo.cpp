// Offline algorithm exercise only. This is not an importer or network endpoint.
#include "care_footprint.h"
#include "care_temporal.h"
#include "care_trending.h"
#include <QCoreApplication>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cstdio>
#include <stdexcept>

using namespace care_contract;
using namespace care_algorithms;
namespace {
void require(bool ok, const QString& message) {
    if (!ok) throw std::runtime_error(message.toUtf8().constData());
}
QString string(const QJsonObject& object, const QString& key) {
    require(object.value(key).isString(), QStringLiteral("Expected string: ") + key);
    return object.value(key).toString();
}
qint64 integer(const QJsonObject& object, const QString& key) {
    const auto value = string(object, key);
    bool ok = false;
    const auto result = value.toLongLong(&ok);
    require(ok && result >= 0 && QString::number(result) == value, QStringLiteral("Invalid decimal: ") + key);
    return result;
}
template <typename T> T checked(const Result<T>& result) {
    require(result.status.ok() && result.value.has_value(), result.status.message);
    return *result.value;
}
QJsonArray recommendations(const TemporalRecommendationResult& result) {
    QJsonArray output;
    for (const auto& item : result.items)
        output.append(QJsonObject{{"work_id", item.work.resource.workId}, {"title", item.work.media.fileName},
                                  {"category", item.work.media.category}, {"reason", item.reason}});
    return output;
}
QJsonObject footprint(const PeriodFootprint& value) {
    QJsonArray categories;
    for (const auto& bucket : value.categories)
        categories.append(QJsonObject{{"category", bucket.label}, {"count", QString::number(bucket.count)}});
    return {{"views", QString::number(value.views)}, {"searches", QString::number(value.searches)},
            {"unique_works", QString::number(value.uniqueWorks)}, {"active_days", QString::number(value.activeDays)},
            {"day_count", value.days.size()}, {"categories", categories}};
}
}

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    try {
        require(application.arguments().size() == 2 || application.arguments().size() == 3,
                QStringLiteral("Usage: care_simulation_demo algorithm_fixture.json [report.json]"));
        QFile input(application.arguments().at(1));
        require(input.open(QIODevice::ReadOnly), QStringLiteral("Cannot open fixture"));
        QJsonParseError parse;
        const auto document = QJsonDocument::fromJson(input.readAll(), &parse);
        require(parse.error == QJsonParseError::NoError && document.isObject(), QStringLiteral("Invalid fixture JSON"));
        const auto data = document.object();
        require(data.value("schema_version").toInt() == 1 && data.value("synthetic").toBool(), QStringLiteral("Synthetic v1 fixture required"));
        const auto now = integer(data, "now_utc");
        const auto start = integer(data, "start_utc");
        PublicSnapshot publicData;
        publicData.nowUtc = now;
        QHash<QString, ResourceSummary> works;
        for (const auto& value : data.value("works").toArray()) {
            const auto object = value.toObject();
            ResourceSummary work;
            const auto source = object.value("source").toObject();
            work.resource = {string(object, "work_id"), {integer(source, "warehouse_id"), integer(source, "item_id")}};
            work.media.id = string(object, "id");
            work.media.fileName = string(object, "file_name");
            work.media.category = string(object, "category");
            work.media.fileType = string(object, "file_type");
            work.media.releaseDate = QDate::fromString(string(object, "release_date"), Qt::ISODate);
            // The CSV's names are not canonical identities; people intentionally empty.
            require(!works.contains(work.resource.workId), QStringLiteral("Duplicate work"));
            works.insert(work.resource.workId, work);
            publicData.publicWorks.append(work);
        }
        QHash<QString, QList<ObservedBehavior>> histories;
        for (const auto& user : data.value("users").toArray()) {
            const auto id = string(user.toObject(), "user_ref");
            require(!histories.contains(id), QStringLiteral("Duplicate user"));
            histories.insert(id, {});
        }
        for (const auto& value : data.value("events").toArray()) {
            const auto object = value.toObject();
            ObservedBehavior event;
            event.userKey = string(object, "user_ref");
            require(histories.contains(event.userKey), QStringLiteral("Unknown user"));
            event.record.event.eventId = string(object, "event_id");
            event.record.occurredAtUtc = integer(object, "occurred_at_utc");
            require(object.value("synthetic").toBool(), QStringLiteral("All demo events must be synthetic"));
            event.record.synthetic = true;
            event.publicEligible = object.value("public_eligible").toBool();
            const auto action = string(object, "action");
            if (action == "view") {
                const auto id = string(object, "work_id");
                require(works.contains(id), QStringLiteral("Unknown work"));
                event.record.event.action = BehaviorAction::View;
                event.record.event.resource = works.value(id).resource;
                event.workSnapshot = works.value(id);
            } else {
                require(action == "search", QStringLiteral("Unknown action"));
                event.record.event.action = BehaviorAction::Search;
            }
            event.record.event.query = string(object, "query");
            histories[event.userKey].append(event);
            publicData.events.append(event);
        }
        TrendingRequest trendRequest{TrendingKind::Works, {now - 7 * 86400, now}, 50};
        const auto trending = checked(buildTrending(trendRequest, publicData));
        QJsonArray trendingItems;
        QHash<QString, qint64> heat;
        for (const auto& item : trending.items) {
            heat.insert(item.work->resource.workId, item.effectiveEvents);
            trendingItems.append(QJsonObject{{"work_id", item.work->resource.workId},
                {"distinct_users", QString::number(item.distinctUsers)}, {"effective_events", QString::number(item.effectiveEvents)}});
        }
        QJsonArray users;
        auto userIds = histories.keys();
        std::sort(userIds.begin(), userIds.end());
        for (const auto& id : userIds) {
            PersonalSnapshot personal;
            personal.userKey = id;
            personal.nowUtc = now;
            personal.events = histories.value(id);
            personal.readableWorks = publicData.publicWorks;
            personal.publicHeat = heat;
            personal.publicHeatContainsSyntheticData = trending.containsSyntheticData;
            const auto report = checked(buildFootprint({{start, now}, {}}, personal));
            const auto recent = checked(buildFootprint({{now - 14 * 86400, now}, TimeRange{now - 28 * 86400, now - 14 * 86400}}, personal));
            const auto temporal = checked(recommendTemporal({}, personal));
            auto before = personal;
            before.nowUtc = now - 14 * 86400;
            before.publicHeat.clear(); // Never leak the later week's heat into this earlier snapshot.
            before.publicHeatContainsSyntheticData = false;
            const auto earlier = checked(recommendTemporal({}, before));
            // A labelled ablation: cumulative category/person interest, no heat or date.
            // No scenario/profile labels are read by any algorithm.
            TemporalScoringConfig cumulative;
            cumulative.longHalfLifeDays = 1e12;
            cumulative.longWeight = 1;
            cumulative.recentWeight = cumulative.heatWeight = cumulative.freshnessWeight = 0;
            const auto baseline = checked(recommendTemporal({}, personal, cumulative));
            auto recentOnly = cumulative;
            recentOnly.longWeight = 0;
            recentOnly.recentWeight = 1;
            const auto recentInterest = checked(recommendTemporal({}, personal, recentOnly));
            users.append(QJsonObject{{"user_ref", id}, {"footprint", footprint(report.current)},
                {"last_14_days", footprint(recent.current)}, {"previous_14_days", footprint(*recent.comparison)},
                {"temporal", recommendations(temporal)}, {"two_weeks_earlier", recommendations(earlier)},
                {"cumulative_interest", recommendations(baseline)}, {"recent_interest_only", recommendations(recentInterest)},
                {"contains_synthetic_data", temporal.containsSyntheticData}});
        }
        const QJsonObject output{{"synthetic", true}, {"user_count", users.size()},
            {"event_count", publicData.events.size()}, {"now_utc", QString::number(now)},
            {"heat_scope", "published_top_50_last_7_days"}, {"trending", trendingItems},
            {"trending_synthetic", trending.containsSyntheticData}, {"users", users}};
        QFile standardOutput;
        if (application.arguments().size() == 3) {
            standardOutput.setFileName(application.arguments().at(2));
            require(!standardOutput.exists(), QStringLiteral("Report already exists"));
            require(standardOutput.open(QIODevice::WriteOnly | QIODevice::NewOnly), QStringLiteral("Cannot create report"));
        } else {
            require(standardOutput.open(stdout, QIODevice::WriteOnly), QStringLiteral("Cannot write report"));
        }
        const auto bytes = QJsonDocument(output).toJson(QJsonDocument::Indented);
        require(standardOutput.write(bytes) == bytes.size() && standardOutput.flush(), QStringLiteral("Cannot finish writing report"));
        return 0;
    } catch (const std::exception& error) {
        fprintf(stderr, "simulation error: %s\n", error.what());
        return 1;
    }
}
