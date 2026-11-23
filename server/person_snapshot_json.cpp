// >>> MAIN-DEFENSE | 主逻辑文件导读
// 历史人物快照序列化：保存事件发生时已确认的人物信息，读取时验证人物ID/角色与作品来源，不能用今天的目录冒充历史事实。
// 阅读时先看函数入口说明，再按“处理阶段”往下；到每个return核对成功、空结果和错误是否区别。
// <<< MAIN-DEFENSE
#include "person_snapshot_json.h"

namespace person_snapshot_json {
using namespace care_contract;
// >>> MAIN-DEFENSE | 函数入口｜encode
// 将已校验业务对象编码为JSON，保留稳定标识和缺失字段语义。
// 签名提示：QJsonArray encode(const QList<PersonSummary>& people) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
QJsonArray encode(const QList<PersonSummary>& people) {
    QJsonArray result;
    for (const auto& person : people) {
        QJsonArray roles;
        // >>> MAIN-DEFENSE | encode｜处理阶段 01
        // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
        // <<< MAIN-DEFENSE
        for (const auto role : person.roles)
            roles.append(role == PersonRole::Actor ? "actor" : role == PersonRole::Director ? "director" : "musician");
        // A follow flag is current user state, not a historical property of the person.
        result.append(QJsonObject{{"person_id", person.personId}, {"name", person.name}, {"roles", roles}});
    }
    // >>> MAIN-DEFENSE | encode｜处理阶段 02
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return result;
}
// >>> MAIN-DEFENSE | 函数入口｜decode
// 将JSON解码并校验字段类型及来源，失败返回空或错误，不擅自补造身份。
// 签名提示：std::optional<QList<PersonSummary>> decode(const QJsonObject& snapshot, const ResourceRef& ref, const MediaRecord& media) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
std::optional<QList<PersonSummary>> decode(const QJsonObject& snapshot, const ResourceRef& ref, const MediaRecord& media) {
    if (!snapshot.contains(Field)) return QList<PersonSummary>{};
    const auto value = snapshot.value(Field);
    if (!value.isArray() || value.toArray().size() > care_algorithms::MaxConfirmedPeoplePerWork) return {};
    QList<PersonSummary> people;
    // >>> MAIN-DEFENSE | decode｜处理阶段 01
    // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
    // <<< MAIN-DEFENSE
    for (const auto& row : value.toArray()) {
        if (!row.isObject()) return {};
        const auto object = row.toObject();
        if (!object.value("person_id").isString() || !object.value("name").isString() ||
            !object.value("roles").isArray() || object.value("name").toString().size() > MaxQueryUnits ||
            object.value("roles").toArray().size() > 3) return {};
        PersonSummary person;
        person.personId = object.value("person_id").toString();
        person.name = object.value("name").toString();
        // >>> MAIN-DEFENSE | decode｜处理阶段 02
        // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
        // <<< MAIN-DEFENSE
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
    // >>> MAIN-DEFENSE | decode｜处理阶段 03
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!checked.status.ok() || !checked.value) return {};
    return checked.value->readableWorks.first().people;
}
}
