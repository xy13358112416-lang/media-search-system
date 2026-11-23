// >>> DEFENSE-NOTE | 头文件导读｜媒体助手交互声明
// 收集用户需求、类别与情绪/场景，将选择交给上层回调；对话框不持有模型密钥。
// 读法：struct说明传哪些数据；class/函数声明说明谁可调用；实际算法去同名.cpp或引用的实现文件。
// <<< DEFENSE-NOTE | 本段讲解结束
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
