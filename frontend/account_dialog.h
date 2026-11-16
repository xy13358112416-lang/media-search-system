#pragma once

#include "account_history_contract.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace account_ui {

inline bool signIn(account_contract::AccountHistoryService& service)
{
    using account_contract::UserRole;
    const auto initial = service.needsInitialAdmin();
    if (!initial.status.ok() || !initial.value) {
        QMessageBox::warning(nullptr, QStringLiteral("账户初始化失败"), initial.status.message);
        return false;
    }
    bool needsAdmin = *initial.value;
    enum class Form { UserLogin, AdminLogin, RegisterUser, CreateAdmin };
    auto currentForm = Form::UserLogin;
    QDialog dialog;
    dialog.setMinimumWidth(420);
    auto* layout = new QVBoxLayout(&dialog);
    auto* pages = new QStackedWidget;
    layout->addWidget(pages);

    auto* entry = new QWidget;
    auto* entryLayout = new QVBoxLayout(entry);
    auto* heading = new QLabel(QStringLiteral("选择登录方式"));
    heading->setObjectName(QStringLiteral("dialogTitle"));
    entryLayout->addWidget(heading);
    auto* chooseUser = new QPushButton(QStringLiteral("用户登录"));
    auto* chooseAdmin = new QPushButton(QStringLiteral("管理员登录"));
    auto* cancel = new QPushButton(QStringLiteral("取消"));
    for (auto* button : {chooseUser, chooseAdmin, cancel}) {
        button->setAutoDefault(false);
        entryLayout->addWidget(button);
    }
    pages->addWidget(entry);

    auto* admin = new QWidget;
    auto* adminLayout = new QVBoxLayout(admin);
    auto* adminHeading = new QLabel(QStringLiteral("管理员登录"));
    adminHeading->setObjectName(QStringLiteral("dialogTitle"));
    adminLayout->addWidget(adminHeading);
    auto* adminNote = new QLabel;
    adminNote->setWordWrap(true);
    adminNote->setTextFormat(Qt::PlainText);
    adminLayout->addWidget(adminNote);
    auto* adminLogin = new QPushButton(QStringLiteral("登录管理员"));
    auto* createAdmin = new QPushButton(QStringLiteral("创建管理员"));
    auto* adminBack = new QPushButton(QStringLiteral("返回"));
    for (auto* button : {adminLogin, createAdmin, adminBack}) {
        button->setAutoDefault(false);
        adminLayout->addWidget(button);
    }
    pages->addWidget(admin);

    auto* credentials = new QWidget;
    auto* credentialsLayout = new QVBoxLayout(credentials);
    auto* form = new QFormLayout;
    auto* username = new QLineEdit;
    username->setObjectName(QStringLiteral("loginUsername"));
    auto* password = new QLineEdit;
    password->setObjectName(QStringLiteral("loginPassword"));
    auto* confirm = new QLineEdit;
    confirm->setObjectName(QStringLiteral("loginConfirmPassword"));
    for (auto* input : {password, confirm}) {
        input->setEchoMode(QLineEdit::Password);
        input->setInputMethodHints(Qt::ImhSensitiveData | Qt::ImhNoAutoUppercase);
    }
    form->addRow(QStringLiteral("用户名"), username);
    form->addRow(QStringLiteral("密码"), password);
    form->addRow(QStringLiteral("确认密码"), confirm);
    credentialsLayout->addLayout(form);
    auto* message = new QLabel;
    message->setTextFormat(Qt::PlainText);
    message->setWordWrap(true);
    credentialsLayout->addWidget(message);
    auto* actions = new QDialogButtonBox;
    auto* submit = actions->addButton(QString(), QDialogButtonBox::ActionRole);
    submit->setObjectName(QStringLiteral("primary"));
    auto* registerUser = actions->addButton(QStringLiteral("注册"), QDialogButtonBox::ActionRole);
    auto* formBack = actions->addButton(QStringLiteral("返回"), QDialogButtonBox::ActionRole);
    registerUser->setAutoDefault(false);
    formBack->setAutoDefault(false);
    credentialsLayout->addWidget(actions);
    pages->addWidget(credentials);

    const auto showEntry = [&] {
        dialog.setWindowTitle(QStringLiteral("选择登录方式"));
        pages->setCurrentWidget(entry);
        username->clear();
        password->clear();
        confirm->clear();
        chooseUser->setFocus();
    };
    const auto showAdmin = [&] {
        const auto state = service.needsInitialAdmin();
        const bool loaded = state.status.ok() && state.value.has_value();
        if (loaded)
            needsAdmin = *state.value;
        dialog.setWindowTitle(QStringLiteral("管理员登录"));
        pages->setCurrentWidget(admin);
        username->clear();
        password->clear();
        confirm->clear();
        createAdmin->setEnabled(loaded && needsAdmin);
        adminLogin->setEnabled(loaded && !needsAdmin);
        if (!loaded) {
            adminNote->setText(state.status.message);
        } else {
            adminNote->setText(needsAdmin ? QStringLiteral("首次使用，请先创建管理员。")
                                          : QStringLiteral("管理员已创建，请使用已有账号登录。"));
            (needsAdmin ? createAdmin : adminLogin)->setFocus();
        }
    };
    const auto showForm = [&](Form page, const QString& feedback = QString()) {
        currentForm = page;
        const bool creating = page == Form::CreateAdmin || page == Form::RegisterUser;
        QString title;
        switch (page) {
        case Form::UserLogin: title = QStringLiteral("用户登录"); break;
        case Form::AdminLogin: title = QStringLiteral("管理员登录"); break;
        case Form::RegisterUser: title = QStringLiteral("注册普通用户"); break;
        case Form::CreateAdmin: title = QStringLiteral("创建管理员"); break;
        }
        dialog.setWindowTitle(title);
        pages->setCurrentWidget(credentials);
        submit->setText(creating ? title : QStringLiteral("登录"));
        submit->setDefault(true);
        registerUser->setVisible(page == Form::UserLogin);
        registerUser->setEnabled(!needsAdmin);
        form->labelForField(confirm)->setVisible(creating);
        confirm->setVisible(creating);
        password->clear();
        confirm->clear();
        message->setText(feedback);
        (username->text().isEmpty() ? username : password)->setFocus();
    };
    QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(chooseUser, &QPushButton::clicked, &dialog, [&] {
        showForm(Form::UserLogin, needsAdmin ? QStringLiteral("首次使用须由管理员完成初始化后，普通用户才可注册。")
                                           : QString());
    });
    QObject::connect(chooseAdmin, &QPushButton::clicked, &dialog, showAdmin);
    QObject::connect(adminLogin, &QPushButton::clicked, &dialog, [&] { showForm(Form::AdminLogin); });
    QObject::connect(createAdmin, &QPushButton::clicked, &dialog, [&] { showForm(Form::CreateAdmin); });
    QObject::connect(adminBack, &QPushButton::clicked, &dialog, showEntry);
    QObject::connect(registerUser, &QPushButton::clicked, &dialog, [&] { showForm(Form::RegisterUser); });
    QObject::connect(formBack, &QPushButton::clicked, &dialog, [&] {
        if (currentForm == Form::CreateAdmin || currentForm == Form::AdminLogin)
            showAdmin();
        else if (currentForm == Form::RegisterUser)
            showForm(Form::UserLogin);
        else
            showEntry();
    });
    QObject::connect(submit, &QPushButton::clicked, &dialog, [&] {
        const auto name = username->text().trimmed();
        const auto secret = password->text();
        if (name.isEmpty() || secret.isEmpty()) {
            message->setText(QStringLiteral("请输入用户名和密码。"));
            return;
        }
        const bool adminLoginSelected = currentForm == Form::AdminLogin;
        if (adminLoginSelected || currentForm == Form::UserLogin) {
            const auto result = service.login(name, secret);
            if (result.status.ok() && result.value) {
                const auto expected = adminLoginSelected ? UserRole::Admin : UserRole::User;
                if (result.value->role == expected) {
                    dialog.accept();
                    return;
                }
                service.logout();
                message->setText(adminLoginSelected ? QStringLiteral("这是普通用户账号，请使用用户登录。")
                                                    : QStringLiteral("这是管理员账号，请使用管理员登录。"));
            } else {
                message->setText(result.status.message.isEmpty() ? QStringLiteral("登录失败。") : result.status.message);
            }
            password->selectAll();
            password->setFocus();
            return;
        }
        if (secret != confirm->text()) {
            message->setText(QStringLiteral("两次输入的密码不一致。"));
            confirm->setFocus();
            return;
        }
        const bool creatingAdmin = currentForm == Form::CreateAdmin;
        const auto result = creatingAdmin ? service.createInitialAdmin(name, secret) : service.registerUser(name, secret);
        if (!result.ok()) {
            message->setText(result.message);
            return;
        }
        if (creatingAdmin)
            needsAdmin = false;
        showForm(creatingAdmin ? Form::AdminLogin : Form::UserLogin, QStringLiteral("账户已创建，请登录。"));
    });
    showEntry();
    return dialog.exec() == QDialog::Accepted;
}

} // namespace account_ui
