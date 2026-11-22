// >>> DEFENSE-NOTE | 头文件导读｜动态推荐页工厂声明
// 依赖CareService和当前场景提供函数；创建页面不意味着立即请求模型。
// 读法：struct说明传哪些数据；class/函数声明说明谁可调用；实际算法去同名.cpp或引用的实现文件。
// <<< DEFENSE-NOTE | 本段讲解结束
#pragma once

#include "care_page.h"

CarePage* createTemporalPage(care_contract::CareService& service, std::function<care_contract::BrowseScene()> scene,
    std::function<void(const care_contract::WarehouseItemRef&)> open = {});
