// >>> DEFENSE-NOTE | 头文件导读｜算法共享清洗函数声明
// normalizeText统一文本，canonicalWorks规范作品，personalEvents/publicEvents验证去重行为。
// 读法：struct说明传哪些数据；class/函数声明说明谁可调用；实际算法去同名.cpp或引用的实现文件。
// <<< DEFENSE-NOTE | 本段讲解结束
#pragma once
#include "care_algorithm_types.h"

namespace care_algorithms::detail {
care_contract::Status serverError(const QString& message);
care_contract::Status validateNow(qint64 nowUtc);
QString normalizeText(const QString& text); // NFKC, casefold, whitespace collapse; preserves punctuation.
// Algorithm candidates require a display title by default. Server adapters may
// validate identity/people/source consistency without adding a title constraint
// to media already accepted by the warehouse contract.
care_contract::Result<QList<care_contract::ResourceSummary>> canonicalWorks(
    const QList<care_contract::ResourceSummary>& works, bool requireTitle = true);
care_contract::Result<QList<ObservedBehavior>> personalEvents(const PersonalSnapshot& snapshot);
care_contract::Result<QList<ObservedBehavior>> publicEvents(const PublicSnapshot& snapshot);
} // namespace care_algorithms::detail
