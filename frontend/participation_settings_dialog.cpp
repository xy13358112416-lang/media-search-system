#include "participation_settings_dialog.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

using namespace care_contract;

namespace {
class ParticipationSettingsDialog final : public QDialog {
public:
    ParticipationSettingsDialog(CareService& service, QWidget* parent)
        : QDialog(parent), service_(service)
    {
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowTitle(QStringLiteral("在线参与设置"));
        setMinimumWidth(430);
        auto* layout = new QVBoxLayout(this);
        status_->setTextFormat(Qt::PlainText);
        status_->setWordWrap(true);
        layout->addWidget(status_);
        layout->addWidget(history_);
        layout->addWidget(personalization_);
        layout->addWidget(publicTrends_);
        auto* explanation = new QLabel(QStringLiteral(
            "公开贡献默认关闭。关闭后，既有历史也不再计入公开热榜。"
            "可以在个人足迹页面清理历史。"));
        explanation->setWordWrap(true);
        layout->addWidget(explanation);
        auto* buttons = new QDialogButtonBox;
        save_ = buttons->addButton(QStringLiteral("保存设置"), QDialogButtonBox::AcceptRole);
        reload_ = buttons->addButton(QStringLiteral("重新读取"), QDialogButtonBox::ResetRole);
        auto* close = buttons->addButton(QStringLiteral("关闭"), QDialogButtonBox::RejectRole);
        layout->addWidget(buttons);
        connect(save_, &QPushButton::clicked, this, [this] { save(); });
        connect(reload_, &QPushButton::clicked, this, [this] { load(); });
        connect(close, &QPushButton::clicked, this, &QDialog::reject);
    }

    void load()
    {
        setReady(false);
        reload_->setEnabled(false);
        status_->setText(QStringLiteral("正在读取服务器设置…"));
        service_.getParticipationSettings([self = QPointer<ParticipationSettingsDialog>(this)](auto result) {
            if (!self || self->finished_)
                return;
            self->reload_->setEnabled(true);
            if (result.status.ok() && result.value) {
                self->saved_ = *result.value;
                self->displaySaved();
                self->status_->setText(QStringLiteral("这些设置适用于当前服务器账号。"));
                self->setReady(true);
            } else {
                self->showError(result.status);
            }
        });
    }

    void done(int result) override
    {
        finished_ = true;
        QDialog::done(result);
    }

private:
    void setReady(bool ready)
    {
        history_->setEnabled(ready);
        personalization_->setEnabled(ready);
        publicTrends_->setEnabled(ready);
        save_->setEnabled(ready);
    }

    void displaySaved()
    {
        history_->setChecked(saved_.historyEnabled);
        personalization_->setChecked(saved_.personalizationEnabled);
        publicTrends_->setChecked(saved_.contributePublicTrends);
    }

    void showError(const Status& error)
    {
        status_->setText(error.message.isEmpty() ? QStringLiteral("无法读取或保存设置。") : error.message);
    }

    void save()
    {
        const ParticipationSettings settings{history_->isChecked(), personalization_->isChecked(),
                                             publicTrends_->isChecked()};
        setReady(false);
        reload_->setEnabled(false);
        status_->setText(QStringLiteral("正在保存设置…"));
        service_.setParticipationSettings(settings, [self = QPointer<ParticipationSettingsDialog>(this)](Status status) {
            if (!self || self->finished_)
                return;
            if (status.ok()) {
                self->accept();
                return;
            }
            self->displaySaved();
            self->reload_->setEnabled(true);
            if (status.code == warehouse_contract::ErrorCode::OutcomeUnknown) {
                self->status_->setText(QStringLiteral("服务器写入结果未知，请先重新读取设置。"));
                return;
            }
            self->setReady(true);
            self->showError(status);
        });
    }

    CareService& service_;
    ParticipationSettings saved_;
    QLabel* status_ = new QLabel;
    QCheckBox* history_ = new QCheckBox(QStringLiteral("保存浏览和搜索历史"));
    QCheckBox* personalization_ = new QCheckBox(QStringLiteral("根据历史生成个性化推荐"));
    QCheckBox* publicTrends_ = new QCheckBox(QStringLiteral("允许行为计入公开热榜"));
    QPushButton* save_ = nullptr;
    QPushButton* reload_ = nullptr;
    bool finished_ = false;
};
}

QDialog* createParticipationSettingsDialog(CareService& service, QWidget* parent)
{
    auto* dialog = new ParticipationSettingsDialog(service, parent);
    dialog->load();
    return dialog;
}
