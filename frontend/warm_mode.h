// >>> DEFENSE-NOTE | 头文件导读｜温暖场景与主题映射
// Scene枚举表达主动选择；browseScene转成业务枚举，theme提供标题/色彩/情绪提示，场景不是系统推断的真实情绪。
// 读法：struct说明传哪些数据；class/函数声明说明谁可调用；实际算法去同名.cpp或引用的实现文件。
// <<< DEFENSE-NOTE | 本段讲解结束
#pragma once

#include "media_mood.h"
#include "care_contract.h"

namespace warm_mode {

enum class Scene { Neutral, Relax, Focus, Explore };

struct Theme {
    QString label;
    QString banner;
    QString surface;
    media_mood::Mood mood = media_mood::Mood::Any;
};

inline care_contract::BrowseScene browseScene(Scene scene)
{
    using care_contract::BrowseScene;
    switch (scene) {
    case Scene::Relax: return BrowseScene::Relax;
    case Scene::Focus: return BrowseScene::Focus;
    case Scene::Explore: return BrowseScene::Explore;
    case Scene::Neutral: return BrowseScene::Neutral;
    }
    return BrowseScene::Neutral;
}

inline Theme theme(Scene scene)
{
    switch (scene) {
    case Scene::Relax:
        return {QStringLiteral("放松"), QStringLiteral("#436355"), QStringLiteral("#e8f0e8"),
                media_mood::Mood::Relaxing};
    case Scene::Focus:
        return {QStringLiteral("专注"), QStringLiteral("#394866"), QStringLiteral("#edf0f8")};
    case Scene::Explore:
        return {QStringLiteral("探索"), QStringLiteral("#785140"), QStringLiteral("#f8ede4")};
    case Scene::Neutral:
        return {QStringLiteral("默认"), QStringLiteral("#173253"), QStringLiteral("#e7eef9")};
    }
    return {};
}

} // namespace warm_mode
