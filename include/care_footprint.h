// >>> DEFENSE-NOTE | 头文件导读｜足迹算法声明
// 输入请求区间与本人快照，输出统计报告；不产生推荐，也不直接读数据库。
// 读法：struct说明传哪些数据；class/函数声明说明谁可调用；实际算法去同名.cpp或引用的实现文件。
// <<< DEFENSE-NOTE | 本段讲解结束
#pragma once

#include "care_algorithm_types.h"

namespace care_algorithms {
// Pure computation over an authenticated, server-created snapshot. No I/O.
// Days follow Asia/Shanghai; each requested period is capped at MaxReportDays.
care_contract::Result<care_contract::FootprintReport> buildFootprint(
    const care_contract::FootprintRequest& request, const PersonalSnapshot& snapshot);
} // namespace care_algorithms
