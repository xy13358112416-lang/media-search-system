// >>> DEFENSE-NOTE | 头文件导读｜足迹页工厂声明
// 用业务服务加载本人统计报告，signedIn用于检查页面操作时的登录状态。
// 读法：struct说明传哪些数据；class/函数声明说明谁可调用；实际算法去同名.cpp或引用的实现文件。
// <<< DEFENSE-NOTE | 本段讲解结束
#pragma once

#include "care_page.h"

CarePage* createFootprintPage(care_contract::CareService& service, std::function<bool()> signedIn = {});
