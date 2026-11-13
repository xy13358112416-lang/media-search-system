#pragma once

#include "account_history_contract.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace account_ui {

inline bool signIn(account_contract::AccountHistoryService& service)
{
    const auto initial = service.needsInitialAdmin();
    if (!initial.status.ok() || !initial.value) {
        QMessageBox::warning(nullptr, QStringLiteral("账户初始化失败"), initial.status.message);
        return false;
    }

    enum class Page { InitialAdmin, Login, Register };
    Page page = *initial.value ? Page::InitialAdmin : Page::Login;
    QDialog dialog;
    dialog.setMinimumWidth(420);
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    auto* username = new QLineEdit;
    auto* password = new QLineEdit;
    auto* confirm = new QLineEdit;
    for (auto* input : {password, confirm}) {
        input->setEchoMode(QLineEdit::Password);
        input->setInputMethodHints(Qt::ImhSensitiveData | Qt::ImhNoAutoUppercase);
    }
    form->addRow(QStringLiteral("用户名"), username);
    form->addRow(QStringLiteral("密码"), password);
    form->addRow(QStringLiteral("确认密码"), confirm);
    layout->addLayout(form);
    auto* message = new QLabel;
    message->setTextFormat(Qt::PlainText);
    message->setWordWrap(true);
    layout->addWidget(message);
    auto* actions = new QDialogButtonBox(QDialogButtonBox::Cancel);
    auto* submit = actions->addButton(QString(), QDialogButtonBox::ActionRole);
    submit->setDefault(true);
    submit->setObjectName(QStringLiteral("primary"));
    auto* switchPage = actions->addButton(QString(), QDialogButtonBox::ActionRole);
    actions->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addWidget(actions);

    const auto showPage = [&](const QString& feedback = QString()) {
        const QString title = page == Page::InitialAdmin ? QStringLiteral("创建管理员")
            : page == Page::Register ? QStringLiteral("注册普通用户") : QStringLiteral("登录");
        dialog.setWindowTitle(title);
        submit->setText(title);
        switchPage->setVisible(page != Page::InitialAdmin);
        switchPage->setText(page == Page::Login ? QStringLiteral("注册") : QStringLiteral("返回登录"));
        form->labelForField(confirm)->setVisible(page != Page::Login);
        confirm->setVisible(page != Page::Login);
        password->clear();
        confirm->clear();
        message->setText(feedback);
        (username->text().isEmpty() ? username : password)->setFocus();
    };
    QObject::connect(actions, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(switchPage, &QPushButton::clicked, &dialog, [&] {
        page = page == Page::Login ? Page::Register : Page::Login;
        showPage();
    });
    QObject::connect(submit, &QPushButton::clicked, &dialog, [&] {
        const auto name = username->text().trimmed();
        const auto secret = password->text();
        if (name.isEmpty() || secret.isEmpty()) {
            message->setText(QStringLiteral("请输入用户名和密码。"));
            return;
        }
        if (page == Page::Login) {
            const auto result = service.login(name, secret);
            if (result.status.ok() && result.value) {
                dialog.accept();
                return;
            }
            message->setText(result.status.message.isEmpty() ? QStringLiteral("登录失败。") : result.status.message);
            password->selectAll();
            password->setFocus();
            return;
        }
        if (secret != confirm->text()) {
            message->setText(QStringLiteral("两次输入的密码不一致。"));
            confirm->setFocus();
            return;
        }
        const auto result = page == Page::InitialAdmin
            ? service.createInitialAdmin(name, secret) : service.registerUser(name, secret);
        if (!result.ok()) {
            message->setText(result.message);
            return;
        }
        page = Page::Login;
        showPage(QStringLiteral("账户已创建，请登录。"));
    });
    showPage();
    return dialog.exec() == QDialog::Accepted;
}

} // namespace account_ui
