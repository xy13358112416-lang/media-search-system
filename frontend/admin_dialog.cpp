#include "admin_dialog.h"
#include "warehouse_client.h"
#include "online_care_client.h"
#include <QDialog>
#include <QHeaderView>
#include <QGridLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
class AdminDialog final : public QDialog {
public:
    AdminDialog(WarehouseClient& client, QWidget* parent) : QDialog(parent), client_(client)
    {
        setWindowTitle(QStringLiteral("用户管理"));
        resize(660, 480);
        auto* layout = new QVBoxLayout(this);
        status_ = new QLabel;
        status_->setTextFormat(Qt::PlainText);
        status_->setWordWrap(true);
        layout->addWidget(status_);
        table_ = new QTableWidget(0, 4);
        table_->setHorizontalHeaderLabels({QStringLiteral("编号"), QStringLiteral("用户名"),
            QStringLiteral("角色"), QStringLiteral("状态")});
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        table_->setSelectionMode(QAbstractItemView::SingleSelection);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        layout->addWidget(table_);
        auto* buttons = new QGridLayout;
        int buttonIndex = 0;
        layout->addLayout(buttons);
        auto add = [&](const QString& text, auto callback) {
            auto* button = new QPushButton(text);
            buttons->addWidget(button, buttonIndex / 4, buttonIndex % 4);
            ++buttonIndex;
            connect(button, &QPushButton::clicked, this, callback);
        };
        add(QStringLiteral("刷新"), [this] { offset_ = 0; load(); });
        add(QStringLiteral("上一页"), [this] { offset_ = qMax(0, offset_ - 100); load(); });
        next_ = new QPushButton(QStringLiteral("下一页"));
        buttons->addWidget(next_, buttonIndex / 4, buttonIndex % 4);
        ++buttonIndex;
        connect(next_, &QPushButton::clicked, this, [this] { offset_ += 100; load(); });
        add(QStringLiteral("创建用户"), [this] { create(); });
        add(QStringLiteral("启停账号"), [this] { change(false); });
        add(QStringLiteral("重置密码"), [this] { change(true); });
        add(QStringLiteral("关闭"), [this] { reject(); });
        load();
    }
private:
    void load()
    {
        const auto generation = ++generation_;
        table_->setRowCount(0);
        next_->setEnabled(false);
        status_->setText(QStringLiteral("正在读取用户…"));
        const QPointer<AdminDialog> guard(this);
        client_.adminRequest("admin_list_users", {{"offset", QString::number(offset_)}},
            [guard, generation](auto result) {
                if (!guard || generation != guard->generation_) return;
                if (!result.status.ok() || !result.value) {
                    guard->status_->setText(result.status.message); return;
                }
                if (!result.value->value("users").isArray() || !result.value->value("has_more").isBool()) {
                    guard->status_->setText(QStringLiteral("用户列表响应格式无效。")); return;
                }
                const auto users = result.value->value("users").toArray();
                for (const auto& entry : users) {
                    const auto user = entry.toObject();
                    if (!user.value("user_id").isString() || !user.value("username").isString() ||
                        !user.value("is_admin").isBool() || !user.value("disabled").isBool()) {
                        guard->table_->setRowCount(0);
                        guard->status_->setText(QStringLiteral("用户记录格式无效。")); return;
                    }
                    const auto row = guard->table_->rowCount();
                    guard->table_->insertRow(row);
                    const QStringList values{user.value("user_id").toString(), user.value("username").toString(),
                        user.value("is_admin").toBool() ? QStringLiteral("管理员") : QStringLiteral("用户"),
                        user.value("disabled").toBool() ? QStringLiteral("停用") : QStringLiteral("正常")};
                    for (int col = 0; col < values.size(); ++col)
                        guard->table_->setItem(row, col, new QTableWidgetItem(values[col]));
                    guard->table_->item(row, 0)->setData(Qt::UserRole, user);
                }
                guard->next_->setEnabled(result.value->value("has_more").toBool());
                guard->status_->setText(users.isEmpty() ? QStringLiteral("暂无用户。")
                    : QStringLiteral("第 %1 页，共显示 %2 个账号").arg(guard->offset_ / 100 + 1).arg(users.size()));
            });
    }
    void write(const QString& type, QJsonObject payload)
    {
        if (busy_) return;
        busy_ = true;
        ++generation_;
        status_->setText(QStringLiteral("正在保存…"));
        const QPointer<AdminDialog> guard(this);
        client_.adminRequest(type, payload, [guard](auto result) {
            if (!guard) return;
            guard->busy_ = false;
            if (!result.status.ok()) { guard->status_->setText(result.status.message); return; }
            guard->load();
        });
    }
    void create()
    {
        if (busy_) return;
        bool ok = false;
        const auto name = QInputDialog::getText(this, QStringLiteral("创建用户"), QStringLiteral("用户名"), QLineEdit::Normal, {}, &ok);
        if (!ok || name.trimmed().isEmpty()) return;
        const auto password = QInputDialog::getText(this, QStringLiteral("创建用户"), QStringLiteral("密码，至少 6 字节"), QLineEdit::Password, {}, &ok);
        if (ok) write("admin_create_user", {{"username", name}, {"password", password}});
    }
    void change(bool resetPassword)
    {
        if (busy_ || table_->currentRow() < 0) return;
        const auto user = table_->item(table_->currentRow(), 0)->data(Qt::UserRole).toJsonObject();
        if (user.value("is_admin").toBool()) {
            status_->setText(QStringLiteral("管理员账号由服务器控制台维护。")); return;
        }
        const auto id = user.value("user_id").toString();
        if (resetPassword) {
            bool ok = false;
            const auto password = QInputDialog::getText(this, QStringLiteral("重置密码"),
                QStringLiteral("输入 %1 的新密码").arg(user.value("username").toString()), QLineEdit::Password, {}, &ok);
            if (ok) write("admin_reset_password", {{"user_id", id}, {"password", password}});
        } else if (QMessageBox::question(this, QStringLiteral("修改账号状态"),
            QStringLiteral("确认修改 %1 的状态？现有登录会话将断开。").arg(user.value("username").toString())) == QMessageBox::Yes) {
            write("admin_set_disabled", {{"user_id", id}, {"disabled", !user.value("disabled").toBool()}});
        }
    }
    WarehouseClient& client_;
    QLabel* status_;
    QTableWidget* table_;
    QPushButton* next_;
    int offset_ = 0;
    quint64 generation_ = 0;
    bool busy_ = false;
};
}

void showAdminDialog(WarehouseClient& client, QWidget* parent)
{
    AdminDialog dialog(client, parent);
    QMetaObject::Connection disconnected;
    if (auto* online = dynamic_cast<OnlineCareClient*>(&client))
        disconnected = online->onDisconnected(&dialog, [&dialog] { dialog.reject(); });
    dialog.exec();
    QObject::disconnect(disconnected);
}
