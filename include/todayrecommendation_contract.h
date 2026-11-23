// >>> DEFENSE-NOTE | 头文件导读｜单机今日推荐契约
// 输入用户、偏好、数量和可选种子；结果包含ID顺序、说明与日期。在线页面另有时序候选接线。
// 读法：struct说明传哪些数据；class/函数声明说明谁可调用；实际算法去同名.cpp或引用的实现文件。
// <<< DEFENSE-NOTE | 本段讲解结束
#pragma once

#include "personalrecommendation_contract.h"
#include <QString>
#include <QStringList>
#include <QtGlobal>

// 今日推荐的共享约定：仅声明类型和服务入口，不实现算法。
namespace recommendation_contract {

// >>> DEFENSE-NOTE | 类型说明｜TodayRecommendationRequest
// 单机每日选择：seed=0采用日期，其余种子用于可复现实验；不是用户密码或登录token。
// <<< DEFENSE-NOTE | 本段讲解结束
struct TodayRecommendationRequest {
    qint64 userId = 0;
    PreferenceSelection category; // 默认 Auto
    PreferenceSelection fileType; // 默认 Auto
    int limit = 6;
    quint64 seed = 0;             // 0 表示用当天日期（JulianDay）作种子
};

// >>> DEFENSE-NOTE | 类型说明｜TodayRecommendationResult
// 每日结果带日期标识及推荐顺序；不要把日期相同误解为任意候选变化后也保证不变。
// <<< DEFENSE-NOTE | 本段讲解结束
struct TodayRecommendationResult {
    bool success = false;
    QString error;        // 失败时供界面显示；成功时为空。
    QStringList mediaIds; // 成功时按今日推荐顺序排列；失败时为空。
    QString reason;       // 成功时的推荐依据说明。
    QString dateKey;      // ISO 日期，如 2026-11-14。
};

class TodayRecommendationService {
public:
    virtual ~TodayRecommendationService() = default;

    virtual TodayRecommendationResult recommendToday(const TodayRecommendationRequest& request) = 0;
};

} // namespace recommendation_contract
