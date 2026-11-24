#include "admin_dialog.h"
#include "warehouse_client.h"
#include "online_care_client.h"
#include "password_policy.h"
#include <QDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QGridLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <memory>

namespace {
class AdminDialog final : public QDialog {
public:
    AdminDialog(WarehouseClient& client, QWidget* parent) : QDialog(parent), client_(client)
    {
        setWindowTitle(QStringLiteral("用户管理"));
        resize(700, 500);
        auto* layout = new QVBoxLayout(this);
        status_ = new QLabel;
        status_->setObjectName("adminStatus");
        status_->setTextFormat(Qt::PlainText);
        status_->setWordWrap(true);
        layout->addWidget(status_);
        table_ = new QTableWidget(0, 4);
        table_->setObjectName("adminUserTable");
        table_->setHorizontalHeaderLabels({QStringLiteral("编号"), QStringLiteral("用户名"),
            QStringLiteral("角色"), QStringLiteral("状态")});
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        table_->setSelectionMode(QAbstractItemView::SingleSelection);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        layout->addWidget(table_);
        auto* buttons = new QGridLayout;
        layout->addLayout(buttons);
        int index = 0;
        auto add = [&](const char* name, const QString& text, auto callback) {
            auto* button = new QPushButton(text);
            button->setObjectName(name);
            buttons->addWidget(button, index / 4, index % 4); ++index;
            connect(button, &QPushButton::clicked, this, callback);
            return button;
        };
        refresh_ = add("adminRefresh", QStringLiteral("刷新"), [this] { offset_ = 0; load(); });
        previous_ = add("adminPrevious", QStringLiteral("上一页"), [this] { offset_ = qMax(0, offset_ - 100); load(); });
        next_ = add("adminNext", QStringLiteral("下一页"), [this] { offset_ += 100; load(); });
        create_ = add("adminCreateUser", QStringLiteral("创建用户"), [this] { credentials(false); });
        enable_ = add("adminEnableUser", QStringLiteral("启用账号"), [this] { changeState(false); });
        disable_ = add("adminDisableUser", QStringLiteral("停用账号"), [this] { changeState(true); });
        reset_ = add("adminResetPassword", QStringLiteral("重置密码"), [this] { credentials(true); });
        add("adminClose", QStringLiteral("关闭"), [this] { reject(); });
        connect(table_, &QTableWidget::itemSelectionChanged, this, [this] { updateButtons(); });
        load();
    }
private:
    QJsonObject selected() const
    {
        const auto row = table_->currentRow();
        return row < 0 || !table_->item(row, 0) ? QJsonObject{}
            : table_->item(row, 0)->data(Qt::UserRole).toJsonObject();
    }
    void updateButtons()
    {
        const auto user = selected();
        const bool ordinary = !busy_ && !user.isEmpty() && !user.value("is_admin").toBool();
        enable_->setEnabled(ordinary && user.value("disabled").toBool());
        disable_->setEnabled(ordinary && !user.value("disabled").toBool());
        reset_->setEnabled(ordinary);
        refresh_->setEnabled(!busy_); create_->setEnabled(!busy_);
        previous_->setEnabled(!busy_ && offset_ > 0); next_->setEnabled(!busy_ && hasMore_);
        table_->setEnabled(!busy_);
    }
    void load(const QString& success = {}, const QString& retainId = {})
    {
        if (busy_) return;
        busy_ = true;
        const auto generation = ++generation_;
        table_->setRowCount(0); hasMore_ = false;
        updateButtons();
        status_->setText(success.isEmpty() ? QStringLiteral("正在读取用户…") : success + QStringLiteral("；正在刷新…"));
        const QPointer<AdminDialog> guard(this);
        client_.adminRequest("admin_list_users", {{"offset", QString::number(offset_)}},
            [guard, generation, success, retainId](auto result) {
                if (!guard || generation != guard->generation_) return;
                guard->busy_ = false;
                auto fail = [&](const QString& message) {
                    guard->table_->setRowCount(0);
                    guard->status_->setText(success.isEmpty() ? message : success + QStringLiteral("；刷新失败：") + message);
                    guard->updateButtons();
                };
                if (!result.status.ok() || !result.value) { fail(result.status.message); return; }
                if (!result.value->value("users").isArray() || !result.value->value("has_more").isBool()) {
                    fail(QStringLiteral("用户列表响应格式无效。")); return;
                }
                const auto users = result.value->value("users").toArray();
                int retainedRow = -1;
                for (const auto& entry : users) {
                    const auto user = entry.toObject();
                    if (!user.value("user_id").isString() || !user.value("username").isString() ||
                        !user.value("is_admin").isBool() || !user.value("disabled").isBool()) {
                        fail(QStringLiteral("用户记录格式无效。")); return;
                    }
                    const auto row = guard->table_->rowCount();
                    guard->table_->insertRow(row);
                    const QStringList values{user.value("user_id").toString(), user.value("username").toString(),
                        user.value("is_admin").toBool() ? QStringLiteral("管理员") : QStringLiteral("用户"),
                        user.value("disabled").toBool() ? QStringLiteral("停用") : QStringLiteral("正常")};
                    for (int col = 0; col < values.size(); ++col)
                        guard->table_->setItem(row, col, new QTableWidgetItem(values[col]));
                    guard->table_->item(row, 0)->setData(Qt::UserRole, user);
                    if (values[0] == retainId) retainedRow = row;
                }
                guard->hasMore_ = result.value->value("has_more").toBool();
                if (retainedRow >= 0) guard->table_->selectRow(retainedRow);
                const auto page = users.isEmpty() ? QStringLiteral("暂无用户。")
                    : QStringLiteral("第 %1 页，共显示 %2 个账号").arg(guard->offset_ / 100 + 1).arg(users.size());
                guard->status_->setText(success.isEmpty() ? page : success + QStringLiteral("；") + page);
                guard->updateButtons();
            });
    }
    void credentials(bool resetPassword)
    {
        if (busy_) return;
        const auto user = selected();
        if (resetPassword && (user.isEmpty() || user.value("is_admin").toBool())) return;
        auto* form = new QDialog(this);
        form->setObjectName("adminCredentials");
        form->setAttribute(Qt::WA_DeleteOnClose);
        form->setWindowTitle(resetPassword ? QStringLiteral("重置密码") : QStringLiteral("创建用户"));
        auto* layout = new QVBoxLayout(form);
        auto* fields = new QFormLayout;
        auto* name = new QLineEdit(resetPassword ? user.value("username").toString() : QString{});
        name->setObjectName("adminUsername"); name->setMaxLength(64); name->setReadOnly(resetPassword);
        auto* password = new QLineEdit;
        auto* confirmation = new QLineEdit;
        password->setObjectName("adminPassword"); confirmation->setObjectName("adminConfirmation");
        for (auto* edit : {password, confirmation}) {
            edit->setEchoMode(QLineEdit::Password);
            edit->setMaxLength(2048);
            edit->setInputMethodHints(Qt::ImhSensitiveData | Qt::ImhNoAutoUppercase);
        }
        fields->addRow(QStringLiteral("用户名"), name);
        fields->addRow(QStringLiteral("密码"), password);
        fields->addRow(QStringLiteral("确认密码"), confirmation);
        layout->addLayout(fields);
        auto* hint = new QLabel(QStringLiteral("至少 6 个字符；中文、英文均按字符计数，密码区分大小写。"));
        hint->setWordWrap(true); layout->addWidget(hint);
        auto* error = new QLabel;
        error->setObjectName("adminFormError"); error->setTextFormat(Qt::PlainText); error->setWordWrap(true);
        layout->addWidget(error);
        auto* submit = new QPushButton(resetPassword ? QStringLiteral("确认重置") : QStringLiteral("确认创建"));
        submit->setObjectName("adminSubmit"); submit->setDefault(true); layout->addWidget(submit);
        auto* cancel = new QPushButton(QStringLiteral("取消"));
        cancel->setObjectName("adminCancel"); layout->addWidget(cancel);
        connect(cancel, &QPushButton::clicked, form, &QDialog::reject);
        connect(submit, &QPushButton::clicked, form, [this, form, name, password, confirmation, error, submit, resetPassword, user] {
            if (busy_) return;
            const auto validation = online_password::validationError(password->text());
            if (name->text().trimmed().isEmpty()) { error->setText(QStringLiteral("请输入用户名。")); name->setFocus(); return; }
            if (!validation.isEmpty()) { error->setText(validation); password->setFocus(); return; }
            if (password->text() != confirmation->text()) {
                error->setText(QStringLiteral("两次密码不一致，请重新确认。")); confirmation->setFocus(); return;
            }
            QJsonObject payload{{"password", password->text()}};
            payload.insert(resetPassword ? "user_id" : "username", resetPassword ? user.value("user_id").toString() : name->text());
            busy_ = true; ++generation_; updateButtons(); submit->setEnabled(false);
            error->setText(QStringLiteral("正在保存…"));
            const QPointer<AdminDialog> guard(this);
            const QPointer<QDialog> formGuard(form);
            const auto success = resetPassword ? QStringLiteral("已重置 %1 的密码").arg(name->text())
                                               : QStringLiteral("已创建用户 %1").arg(name->text());
            client_.adminRequest(resetPassword ? "admin_reset_password" : "admin_create_user", payload,
                [guard, formGuard, error, submit, success, user](auto result) {
                    if (!guard) return;
                    guard->busy_ = false; guard->updateButtons();
                    if (!result.status.ok()) {
                        guard->status_->setText(result.status.message);
                        if (formGuard) { error->setText(result.status.message); submit->setEnabled(true); }
                        return;
                    }
                    if (formGuard) formGuard->accept();
                    guard->load(success, user.value("user_id").toString());
                });
        });
        form->open();
    }
    void changeState(bool disabled)
    {
        if (busy_) return;
        const auto user = selected();
        if (user.isEmpty() || user.value("is_admin").toBool() || user.value("disabled").toBool() == disabled) return;
        const auto action = disabled ? QStringLiteral("停用账号") : QStringLiteral("启用账号");
        auto* confirmation = new QMessageBox(QMessageBox::Question, action,
            disabled ? QStringLiteral("确认停用 %1？停用后无法登录，现有登录会话将断开。").arg(user.value("username").toString())
                     : QStringLiteral("确认启用 %1？启用后此用户可以重新登录。").arg(user.value("username").toString()),
            QMessageBox::Yes | QMessageBox::No, this);
        confirmation->setObjectName(disabled ? "adminDisableConfirmation" : "adminEnableConfirmation");
        confirmation->setTextFormat(Qt::PlainText);
        confirmation->setDefaultButton(QMessageBox::No);
        confirmation->setAttribute(Qt::WA_DeleteOnClose);
        const auto generation = generation_;
        connect(confirmation, &QDialog::finished, this, [this, generation, user, disabled](int answer) {
            // Send an explicit desired state only for the exact account that was confirmed.
            if (answer != QMessageBox::Yes || busy_ || generation != generation_ || selected() != user) return;
            busy_ = true; ++generation_; updateButtons(); status_->setText(QStringLiteral("正在保存…"));
            const QPointer<AdminDialog> guard(this);
            client_.adminRequest("admin_set_disabled", {{"user_id", user.value("user_id")}, {"disabled", disabled}},
                [guard, user, disabled](auto result) {
                    if (!guard) return;
                    guard->busy_ = false; guard->updateButtons();
                    if (!result.status.ok()) { guard->status_->setText(result.status.message); return; }
                    guard->load((disabled ? QStringLiteral("已停用 %1") : QStringLiteral("已启用 %1"))
                        .arg(user.value("username").toString()), user.value("user_id").toString());
                });
        });
        confirmation->open();
    }
    WarehouseClient& client_;
    QLabel* status_;
    QTableWidget* table_;
    QPushButton *refresh_, *previous_, *next_, *create_, *enable_, *disable_, *reset_;
    int offset_ = 0;
    quint64 generation_ = 0;
    bool busy_ = false, hasMore_ = false;
};
}

QDialog* createAdminDialog(WarehouseClient& client, QWidget* parent)
{
    auto* dialog = new AdminDialog(client, parent);
    if (auto* online = dynamic_cast<OnlineCareClient*>(&client))
        online->onDisconnected(dialog, [dialog] { dialog->reject(); });
    return dialog;
}

void showAdminDialog(WarehouseClient& client, QWidget* parent)
{
    const std::unique_ptr<QDialog> dialog(createAdminDialog(client, parent));
    dialog->exec();
}
