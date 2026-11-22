// >>> DEFENSE-NOTE | 头文件导读｜热榜页工厂声明
// 页面调用同一会话的服务并展示规则/模型摘要，模型不在客户端直接运行。
// 读法：struct说明传哪些数据；class/函数声明说明谁可调用；实际算法去同名.cpp或引用的实现文件。
// <<< DEFENSE-NOTE | 本段讲解结束
#pragma once

#include "care_page.h"

CarePage* createTrendingPage(care_contract::CareService& service,
    std::function<void(const care_contract::WarehouseItemRef&)> open = {});
