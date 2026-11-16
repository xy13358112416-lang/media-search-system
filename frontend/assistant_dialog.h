#pragma once

#include "warm_mode.h"
#include <QDialog>
#include <QIcon>
#include <functional>

QIcon assistantIcon();

class AssistantDialog final : public QDialog {
public:
    AssistantDialog(QWidget* parent,
                    std::function<void(const QString&, media_mood::Mood, const QString&, bool)> openSearch,
                    std::function<void(warm_mode::Scene)> changeScene,
                    std::function<void()> openRecommendations, const QString& recommendationLabel);
};
