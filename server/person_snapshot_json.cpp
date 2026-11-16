#include "person_snapshot_json.h"

namespace person_snapshot_json {
using namespace care_contract;
QJsonArray encode(const QList<PersonSummary>& people) {
    QJsonArray result;
    for (const auto& person : people) {
        QJsonArray roles;
        for (const auto role : person.roles)
            roles.append(role == PersonRole::Actor ? "actor" : role == PersonRole::Director ? "director" : "musician");
        // A follow flag is current user state, not a historical property of the person.
        result.append(QJsonObject{{"person_id", person.personId}, {"name", person.name}, {"roles", roles}});
    }
    return result;
}
std::optional<QList<PersonSummary>> decode(const QJsonObject& snapshot, const ResourceRef& ref, const MediaRecord& media) {
    if (!snapshot.contains(Field)) return QList<PersonSummary>{};
    const auto value = snapshot.value(Field);
    if (!value.isArray() || value.toArray().size() > care_algorithms::MaxConfirmedPeoplePerWork) return {};
    QList<PersonSummary> people;
    for (const auto& row : value.toArray()) {
        if (!row.isObject()) return {};
        const auto object = row.toObject();
        if (!object.value("person_id").isString() || !object.value("name").isString() ||
            !object.value("roles").isArray() || object.value("name").toString().size() > MaxQueryUnits ||
            object.value("roles").toArray().size() > 3) return {};
        PersonSummary person;
        person.personId = object.value("person_id").toString();
        person.name = object.value("name").toString();
        for (const auto& role : object.value("roles").toArray()) {
            if (!role.isString()) return {};
            if (role.toString() == "actor") person.roles.append(PersonRole::Actor);
            else if (role.toString() == "director") person.roles.append(PersonRole::Director);
            else if (role.toString() == "musician") person.roles.append(PersonRole::Musician);
            else return {};
        }
        people.append(person);
    }
    care_algorithms::PersonContext context;
    context.credits = {{ref, people}};
    care_algorithms::CatalogSnapshot catalog;
    catalog.readableWorks = {{ref, media, {}}};
    const auto checked = care_algorithms::attachConfirmedPeople(catalog, context);
    if (!checked.status.ok() || !checked.value) return {};
    return checked.value->readableWorks.first().people;
}
}
