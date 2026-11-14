#pragma once

#include <QDialog>
#include <QIcon>
#include <QJsonArray>
#include <functional>

class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QPushButton;
class QTextEdit;

QIcon assistantIcon();

class AssistantDialog final : public QDialog {
public:
    AssistantDialog(QWidget* parent, std::function<void(const QString&)> openSearch,
                    std::function<void()> openRecommendations, const QString& recommendationLabel);

private:
    void addMessage(const QString& name, const QString& text);
    void send();
    void setBusy(bool busy);
    void showWelcome();

    QNetworkAccessManager* network_;
    QLineEdit* key_;
    QLineEdit* input_;
    QPushButton* send_;
    QPushButton* reset_;
    QLabel* status_;
    QTextEdit* transcript_;
    QJsonArray history_;
};
