// >>> DEFENSE-NOTE | 头文件导读｜热榜算法声明
// 输入公共事件/资源快照；按用户日封顶、三用户门槛、稳定排序；模型摘要在别的模块。
// 读法：struct说明传哪些数据；class/函数声明说明谁可调用；实际算法去同名.cpp或引用的实现文件。
// <<< DEFENSE-NOTE | 本段讲解结束
#pragma once
#include "care_algorithm_types.h"

namespace care_algorithms {
care_contract::Result<care_contract::TrendingResult> buildTrending(
    const care_contract::TrendingRequest& request, const PublicSnapshot& snapshot);
} // namespace care_algorithms
