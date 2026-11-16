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
