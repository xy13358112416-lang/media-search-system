// >>> DEFENSE-NOTE | 头文件导读｜关注中心页工厂声明
// 关注、人物作品和资讯通过服务访问；不是在页面里直接查询SQLite。
// 读法：struct说明传哪些数据；class/函数声明说明谁可调用；实际算法去同名.cpp或引用的实现文件。
// <<< DEFENSE-NOTE | 本段讲解结束
#pragma once

#include "care_page.h"

using PersonWorkViewer = std::function<void(const care_contract::WarehouseItemRef&,
                                            care_contract::Callback<care_contract::ResourceSummary>)>;

CarePage* createFollowCenter(care_contract::CareService& service, PersonWorkViewer viewWork = {});
