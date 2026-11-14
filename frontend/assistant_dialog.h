#pragma once

#include "media_mood.h"
#include <QDialog>
#include <QIcon>
#include <functional>

QIcon assistantIcon();

class AssistantDialog final : public QDialog {
public:
    AssistantDialog(QWidget* parent,
                    std::function<void(const QString&, media_mood::Mood, const QString&)> openSearch,
                    std::function<void()> openRecommendations, const QString& recommendationLabel);
};
