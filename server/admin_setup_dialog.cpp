#include "admin_setup_dialog.h"
#include "warehouse_server.h"
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

AdminSetupDialog::AdminSetupDialog(const QString& path, QWidget* parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("服务端管理员初始化"));
    setObjectName(QStringLiteral("adminSetupDialog"));
    resize(600, 390);
    setMinimumWidth(440);
    setStyleSheet(QStringLiteral(
        "QDialog{background:#f1f6fc;color:#16324f;}"
        "QLabel{color:#16324f;background:transparent;}"
        "QLineEdit{background:white;color:#16324f;border:1px solid #bfd0e8;border-radius:6px;padding:8px;}"
        "QLineEdit:disabled{background:#edf2f8;color:#586b80;}"
        "QPushButton{background:white;color:#16324f;border:1px solid #bfd0e8;border-radius:6px;padding:8px 14px;}"
        "QPushButton:disabled{color:#65758b;background:#edf2f8;}"
        "QPushButton#createAdministrator{background:#28599a;color:white;}"
        "QPushButton#createAdministrator:disabled{background:#d9e3f0;color:#586b80;}"));
    auto* layout = new QVBoxLayout(this);
    auto* heading = new QLabel(QStringLiteral("创建首个管理员"));
    auto font = heading->font(); font.setPointSize(17); font.setBold(true); heading->setFont(font);
    layout->addWidget(heading);
    auto* note = new QLabel(QStringLiteral("请在服务端电脑使用，并先停止服务器。此工具直接操作选定的本机数据库；已有管理员时不可重复初始化。"));
    note->setWordWrap(true); layout->addWidget(note);
    auto* pathRow = new QHBoxLayout;
    database_ = new QLineEdit(QFileInfo(path).absoluteFilePath());
    database_->setObjectName(QStringLiteral("setupDatabase"));
    database_->setReadOnly(true);
    database_->setToolTip(database_->text());
    auto* browse = new QPushButton(QStringLiteral("选择数据库"));
    pathRow->addWidget(database_, 1); pathRow->addWidget(browse);
    layout->addLayout(pathRow);
    auto* form = new QFormLayout;
    username_ = new QLineEdit; username_->setObjectName(QStringLiteral("setupUsername"));
    username_->setMaxLength(64);
    password_ = new QLineEdit; password_->setObjectName(QStringLiteral("setupPassword"));
    confirmation_ = new QLineEdit; confirmation_->setObjectName(QStringLiteral("setupConfirmation"));
    for (auto* edit : {password_, confirmation_}) {
        edit->setEchoMode(QLineEdit::Password);
        edit->setMaxLength(1024);
        edit->setInputMethodHints(Qt::ImhSensitiveData | Qt::ImhNoAutoUppercase);
    }
    password_->setPlaceholderText(QStringLiteral("至少6字节，密码区分大小写"));
    form->addRow(QStringLiteral("管理员账号"), username_);
    form->addRow(QStringLiteral("密码"), password_);
    form->addRow(QStringLiteral("确认密码"), confirmation_);
    layout->addLayout(form);
    status_ = new QLabel;
    status_->setObjectName(QStringLiteral("setupStatus"));
    status_->setTextFormat(Qt::PlainText); status_->setWordWrap(true);
    layout->addWidget(status_);
    auto* buttons = new QHBoxLayout; buttons->addStretch();
    create_ = new QPushButton(QStringLiteral("创建管理员"));
    create_->setObjectName(QStringLiteral("createAdministrator"));
    create_->setDefault(true);
    auto* close = new QPushButton(QStringLiteral("关闭"));
    buttons->addWidget(create_); buttons->addWidget(close); layout->addLayout(buttons);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(create_, &QPushButton::clicked, this, [this] { submit(); });
    connect(confirmation_, &QLineEdit::returnPressed, this, [this] { if (create_->isEnabled()) submit(); });
    connect(browse, &QPushButton::clicked, this, [this] {
        const auto selected = QFileDialog::getOpenFileName(this, QStringLiteral("选择服务器实际使用的数据库"),
            database_->text(), QStringLiteral("SQLite 数据库 (*.sqlite *.db);;所有文件 (*)"));
        if (selected.isEmpty()) return;
        database_->setText(selected); database_->setToolTip(selected);
        loadDatabase();
    });
    loadDatabase();
}

AdminSetupDialog::~AdminSetupDialog() = default;

void AdminSetupDialog::lockForm(bool locked)
{
    create_->setEnabled(!locked);
    username_->setEnabled(!locked);
    password_->setEnabled(!locked);
    confirmation_->setEnabled(!locked);
}

void AdminSetupDialog::loadDatabase()
{
    password_->clear(); confirmation_->clear();
    server_.reset();
    QString error;
    server_ = std::make_unique<WarehouseServer>();
    if (!server_->openDatabase(database_->text(), error)) {
        lockForm(true); status_->setText(error); return;
    }
    const auto exists = server_->hasAdministrator(error);
    lockForm(!exists || *exists);
    status_->setText(!exists ? error : *exists
        ? QStringLiteral("此数据库已有管理员。请在客户端选择管理员登录；本工具不能重置或覆盖已有账号。")
        : QStringLiteral("请填写管理员账号及两次密码，创建后使用客户端的管理员登录入口。"));
    if (exists && !*exists) username_->setFocus();
}

void AdminSetupDialog::submit()
{
    if (!create_->isEnabled()) return;
    if (username_->text().trimmed().isEmpty() || password_->text().isEmpty()) {
        status_->setText(QStringLiteral("请输入账号和密码。")); return;
    }
    if (password_->text() != confirmation_->text()) {
        status_->setText(QStringLiteral("两次密码不一致，账号未创建。"));
        confirmation_->clear(); confirmation_->setFocus(); return;
    }
    QString error;
    const bool saved = server_->initializeAdministrator(username_->text(), password_->text(), error);
    password_->clear(); confirmation_->clear();
    if (saved) {
        lockForm(true);
        status_->setText(QStringLiteral("管理员创建成功。请关闭此窗口，启动服务器，再从客户端的管理员登录入口登录。"));
    } else {
        status_->setText(error);
        const auto exists = server_->hasAdministrator(error);
        if (!exists || *exists) lockForm(true);
    }
}
