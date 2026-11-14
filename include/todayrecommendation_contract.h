#pragma once

#include "personalrecommendation_contract.h"
#include <QString>
#include <QStringList>
#include <QtGlobal>

// 今日推荐的共享约定：仅声明类型和服务入口，不实现算法。
namespace recommendation_contract {

struct TodayRecommendationRequest {
    qint64 userId = 0;
    PreferenceSelection category; // 默认 Auto
    PreferenceSelection fileType; // 默认 Auto
    int limit = 6;
    quint64 seed = 0;             // 0 表示用当天日期（JulianDay）作种子
};

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
