#include "online_startup.h"

#include <QCoreApplication>
#include <QDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

namespace online_ui {
namespace {

bool validHost(const QString& host)
{
    return !host.isEmpty() && !host.contains(QRegularExpression(QStringLiteral("[\\s/\\\\]")));
}

void readEndpoint(QSettings& settings, const QString& group, ConnectionSettings& result)
{
    const auto host = settings.value(group + QStringLiteral("/host")).toString().trimmed();
    if (validHost(host)) result.host = host;
    bool ok = false;
    const auto port = settings.value(group + QStringLiteral("/port")).toString().toUInt(&ok);
    if (ok && port > 0 && port <= 65535) result.port = static_cast<quint16>(port);
}

class StartupDialog final : public QDialog {
public:
    StartupDialog(warehouse_contract::WarehouseService& service,
                  warehouse_contract::UserInfo& user, QWidget* parent,
                  const QString& configPath)
        : QDialog(parent), service_(service), user_(user)
    {
        setObjectName(QStringLiteral("onlineSignIn"));
        setWindowTitle(QStringLiteral("登录媒体库"));
        setMinimumWidth(400);
        const auto endpoint = loadConnectionSettings(configPath);
        auto* layout = new QVBoxLayout(this);
        auto* heading = new QLabel(QStringLiteral("在线媒体库"), this);
        auto font = heading->font();
        font.setPointSize(font.pointSize() + 4);
        font.setBold(true);
        heading->setFont(font);
        layout->addWidget(heading);
        status_ = new QLabel(QStringLiteral("正在连接服务器…"), this);
        status_->setObjectName(QStringLiteral("onlineStatus"));
        status_->setTextFormat(Qt::PlainText);
        status_->setWordWrap(true);
        layout->addWidget(status_);

        auto* credentials = new QFormLayout;
        username_ = new QLineEdit(this);
        username_->setObjectName(QStringLiteral("onlineUsername"));
        username_->setText(QSettings().value(QStringLiteral("online/lastUsername")).toString());
        password_ = new QLineEdit(this);
        password_->setObjectName(QStringLiteral("onlinePassword"));
        password_->setEchoMode(QLineEdit::Password);
        password_->setInputMethodHints(Qt::ImhSensitiveData | Qt::ImhNoAutoUppercase);
        credentials->addRow(QStringLiteral("用户名"), username_);
        credentials->addRow(QStringLiteral("密码"), password_);
        layout->addLayout(credentials);

        auto* changeServer = new QPushButton(QStringLiteral("更改服务器"), this);
        changeServer->setObjectName(QStringLiteral("onlineChangeServer"));
        layout->addWidget(changeServer, 0, Qt::AlignLeft);
        serverSettings_ = new QWidget(this);
        serverSettings_->setObjectName(QStringLiteral("onlineServerSettings"));
        auto* serverForm = new QFormLayout(serverSettings_);
        serverForm->setContentsMargins(0, 0, 0, 0);
        host_ = new QLineEdit(endpoint.host, serverSettings_);
        host_->setObjectName(QStringLiteral("onlineHost"));
        port_ = new QSpinBox(serverSettings_);
        port_->setObjectName(QStringLiteral("onlinePort"));
        port_->setRange(1, 65535);
        port_->setValue(endpoint.port);
        auto* connectButton = new QPushButton(QStringLiteral("重新连接"), serverSettings_);
        connectButton->setObjectName(QStringLiteral("onlineConnect"));
        serverForm->addRow(QStringLiteral("服务器地址"), host_);
        serverForm->addRow(QStringLiteral("端口"), port_);
        serverForm->addRow(connectButton);
        layout->addWidget(serverSettings_);
        serverSettings_->hide();

        auto* buttons = new QHBoxLayout;
        buttons->addStretch();
        auto* cancel = new QPushButton(QStringLiteral("退出"), this);
        cancel->setObjectName(QStringLiteral("onlineCancel"));
        login_ = new QPushButton(QStringLiteral("登录"), this);
        login_->setObjectName(QStringLiteral("onlineLogin"));
        login_->setDefault(true);
        login_->setEnabled(false);
        buttons->addWidget(cancel);
        buttons->addWidget(login_);
        layout->addLayout(buttons);
        connect(changeServer, &QPushButton::clicked, this, [this] {
            serverSettings_->setVisible(!serverSettings_->isVisible());
        });
        connect(connectButton, &QPushButton::clicked, this, [this] { connectToServer(); });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(login_, &QPushButton::clicked, this, [this] { submit(); });
        connect(password_, &QLineEdit::returnPressed, this, [this] { submit(); });
        QTimer::singleShot(0, this, [this] { connectToServer(); });
    }

    ~StartupDialog() override { discard(); }

    void reject() override
    {
        discard();
        QDialog::reject();
    }

private:
    void discard()
    {
        if (finished_) return;
        finished_ = true;
        ++epoch_;
        password_->clear();
        service_.disconnectFromServer();
    }

    void showError(const QString& message)
    {
        status_->setText(message);
        serverSettings_->show();
    }

    void connectToServer()
    {
        const auto attempt = ++epoch_;
        connected_ = false;
        busy_ = false;
        login_->setEnabled(false);
        username_->setEnabled(true);
        password_->setEnabled(true);
        password_->clear();
        service_.disconnectFromServer();
        const auto host = host_->text().trimmed();
        if (!validHost(host)) {
            showError(QStringLiteral("请输入有效的服务器地址。"));
            host_->setFocus();
            return;
        }
        const auto port = static_cast<quint16>(port_->value());
        status_->setText(QStringLiteral("正在连接 %1:%2…").arg(host).arg(port));
        const QPointer<StartupDialog> guard(this);
        service_.connectToServer(host, port, [guard, attempt, host, port](auto result) {
            if (!guard || guard->finished_ || guard->epoch_ != attempt) return;
            if (!result.ok()) {
                guard->showError(QStringLiteral("连接失败：%1").arg(result.message));
                return;
            }
            guard->connected_ = true;
            guard->connectedEndpoint_ = {host, port};
            guard->status_->setText(QStringLiteral("已连接 %1:%2，请登录。没有账号请联系管理员。")
                                       .arg(host).arg(port));
            guard->login_->setEnabled(true);
            if (guard->username_->text().isEmpty()) guard->username_->setFocus();
            else guard->password_->setFocus();
        });
    }

    void submit()
    {
        if (!connected_ || busy_ || finished_) return;
        const auto username = username_->text().trimmed();
        const auto password = password_->text();
        if (username.isEmpty() || password.isEmpty()) {
            status_->setText(QStringLiteral("请输入用户名和密码。"));
            return;
        }
        password_->clear();
        busy_ = true;
        login_->setEnabled(false);
        username_->setEnabled(false);
        password_->setEnabled(false);
        status_->setText(QStringLiteral("正在登录…"));
        const auto attempt = epoch_;
        const QPointer<StartupDialog> guard(this);
        service_.login(username, password, [guard, attempt, username](auto result) {
            if (!guard || guard->finished_ || guard->epoch_ != attempt) return;
            guard->busy_ = false;
            guard->username_->setEnabled(true);
            guard->password_->setEnabled(true);
            if (!result.status.ok() || !result.value) {
                if (result.status.code == warehouse_contract::ErrorCode::NotConnected)
                    guard->connected_ = false;
                guard->login_->setEnabled(guard->connected_);
                guard->showError(QStringLiteral("登录失败：%1").arg(result.status.message));
                guard->password_->setFocus();
                return;
            }
            guard->user_ = *result.value;
            QSettings saved;
            saved.setValue(QStringLiteral("online/host"), guard->connectedEndpoint_.host);
            saved.setValue(QStringLiteral("online/port"), guard->connectedEndpoint_.port);
            saved.setValue(QStringLiteral("online/lastUsername"), username);
            guard->finished_ = true;
            ++guard->epoch_;
            guard->accept();
        });
    }

    warehouse_contract::WarehouseService& service_;
    warehouse_contract::UserInfo& user_;
    QLabel* status_ = nullptr;
    QLineEdit* username_ = nullptr;
    QLineEdit* password_ = nullptr;
    QLineEdit* host_ = nullptr;
    QSpinBox* port_ = nullptr;
    QPushButton* login_ = nullptr;
    QWidget* serverSettings_ = nullptr;
    ConnectionSettings connectedEndpoint_;
    quint64 epoch_ = 0;
    bool connected_ = false;
    bool busy_ = false;
    bool finished_ = false;
};

} // namespace

ConnectionSettings loadConnectionSettings(const QString& configPath)
{
    ConnectionSettings result;
    const auto path = configPath.isEmpty()
        ? QCoreApplication::applicationDirPath() + QStringLiteral("/media-client.ini")
        : configPath;
    QSettings file(path, QSettings::IniFormat);
    readEndpoint(file, QStringLiteral("server"), result);
    QSettings remembered;
    readEndpoint(remembered, QStringLiteral("online"), result);
    return result;
}

bool signIn(warehouse_contract::WarehouseService& service, warehouse_contract::UserInfo& user,
            QWidget* parent, const QString& configPath)
{
    StartupDialog dialog(service, user, parent, configPath);
    return dialog.exec() == QDialog::Accepted;
}

}
