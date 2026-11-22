// >>> MAIN-DEFENSE | 主逻辑头文件导读
// 历史人物快照序列化：保存事件发生时已确认的人物信息，读取时验证人物ID/角色与作品来源，不能用今天的目录冒充历史事实。
// 阅读顺序：输入数据结构 → 返回状态/可选数据 → 方法声明 → 私有成员。引用/指针保存依赖，必须确认由谁拥有及何时释放。
// <<< MAIN-DEFENSE
#pragma once
#include "care_person_snapshot.h"
#include <QJsonArray>
#include <QJsonObject>

// Internal media_snapshot extension only. Not accepted from record_behavior payloads.
// Missing field is a legacy/unknown snapshot; malformed present fields fail the read.
namespace person_snapshot_json {
inline constexpr auto Field = "_care_people_v1";
// >>> MAIN-DEFENSE | 方法声明｜encode
// 将已校验业务对象编码为JSON，保留稳定标识和缺失字段语义。
// <<< MAIN-DEFENSE
QJsonArray encode(const QList<care_contract::PersonSummary>& people);
std::optional<QList<care_contract::PersonSummary>> decode(
    const QJsonObject& snapshot, const care_contract::ResourceRef& ref, const MediaRecord& media);
}
