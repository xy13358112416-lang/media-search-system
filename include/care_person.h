// >>> DEFENSE-NOTE | 头文件导读｜人物查询与关注建议算法声明
// 只查询当前可见可信人物；角色可多值、同名可不同ID；写关注与数据库由服务端负责。
// 读法：struct说明传哪些数据；class/函数声明说明谁可调用；实际算法去同名.cpp或引用的实现文件。
// <<< DEFENSE-NOTE | 本段讲解结束
#pragma once
#include "care_algorithm_types.h"

namespace care_algorithms {
care_contract::Result<care_contract::Page<care_contract::PersonSummary>> searchPeople(
    const care_contract::PersonSearchRequest& request, const CatalogSnapshot& snapshot);
care_contract::Result<care_contract::Page<care_contract::ResourceSummary>> worksForPerson(
    const QString& personId, const care_contract::PageRequest& page, const CatalogSnapshot& snapshot);
// Suggestions never change follows; identity and current readable-work scope are mandatory.
care_contract::Result<QList<care_contract::PersonSummary>> suggestPeople(int limit, const PersonalSnapshot& snapshot);
// Pure selection over a server-managed source cache. Stale records remain usable;
// cacheAvailable=false denotes failure to read the cache, not failure of an external refresh.
care_contract::Result<care_contract::Page<care_contract::PersonUpdate>> personUpdates(
    const care_contract::PersonUpdatesRequest& request, const CatalogSnapshot& snapshot,
    const QList<care_contract::PersonUpdate>& cache, qint64 nowUtc, bool cacheAvailable = true);
} // namespace care_algorithms
