#include "../online_startup.h"
#include "warehouse_client.h"

#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

using namespace warehouse_contract;

namespace {
class StartupClient final : public WarehouseClient {
public:
    void connectToServer(const QString& host, quint16 port, StatusCallback done) override
    {
        hosts.append(host);
        ports.append(port);
        if (deferConnect) connections.append(std::move(done));
        else done(connectStatus);
    }
    void disconnectFromServer() override { ++disconnects; }
    void login(const QString& username, const QString& password, Callback<UserInfo> done) override
    {
        usernames.append(username);
        passwords.append(password);
        if (deferLogin) logins.append(std::move(done));
        else done({{}, UserInfo{42, username}});
    }
    bool deferConnect = false;
    bool deferLogin = false;
    Status connectStatus;
    QStringList hosts, usernames, passwords;
    QList<quint16> ports;
    QList<StatusCallback> connections;
    QList<Callback<UserInfo>> logins;
    int disconnects = 0;
};

template<class T> T* field(QDialog* dialog, const char* name)
{
    return dialog->findChild<T*>(QString::fromLatin1(name));
}

void enterCredentials(QDialog* dialog)
{
    field<QLineEdit>(dialog, "onlineUsername")->setText(QStringLiteral("alice"));
    field<QLineEdit>(dialog, "onlinePassword")->setText(QStringLiteral("private-password"));
    field<QPushButton>(dialog, "onlineLogin")->click();
}
}

class OnlineStartupTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QVERIFY(settingsDirectory.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OnlineStartupTests"));
        QCoreApplication::setApplicationName(QStringLiteral("Startup"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
        watchdog.setSingleShot(true);
        connect(&watchdog, &QTimer::timeout, this, [] {
            if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) dialog->reject();
        });
    }
    void init() { QSettings().clear(); watchdog.start(5000); }
    void cleanup() { watchdog.stop(); }

    void loadsDefaultsAndValidatedIniThenRememberedEndpoint()
    {
        const auto missing = settingsDirectory.filePath("missing.ini");
        auto settings = online_ui::loadConnectionSettings(missing);
        QCOMPARE(settings.host, QStringLiteral("192.168.43.8"));
        QCOMPARE(settings.port, quint16(8765));
        const auto path = settingsDirectory.filePath("server.ini");
        {
            QSettings file(path, QSettings::IniFormat);
            file.setValue("server/host", "  example.test  ");
            file.setValue("server/port", 9876);
        }
        settings = online_ui::loadConnectionSettings(path);
        QCOMPARE(settings.host, QStringLiteral("example.test"));
        QCOMPARE(settings.port, quint16(9876));
        {
            QSettings file(path, QSettings::IniFormat);
            file.setValue("server/host", "  ");
            file.setValue("server/port", 65536);
        }
        settings = online_ui::loadConnectionSettings(path);
        QCOMPARE(settings.host, QStringLiteral("192.168.43.8"));
        QCOMPARE(settings.port, quint16(8765));
        QSettings remembered;
        remembered.setValue("online/host", "remembered.test");
        remembered.setValue("online/port", 5555);
        settings = online_ui::loadConnectionSettings(path);
        QCOMPARE(settings.host, QStringLiteral("remembered.test"));
        QCOMPARE(settings.port, quint16(5555));
        remembered.setValue("online/host", "bad host");
        remembered.setValue("online/port", -1);
        settings = online_ui::loadConnectionSettings(missing);
        QCOMPARE(settings.host, QStringLiteral("192.168.43.8"));
        QCOMPARE(settings.port, quint16(8765));
    }

    void autoConnectsAndKeepsSuccessfulSessionWithoutPersistingPassword()
    {
        StartupClient client;
        UserInfo user;
        bool inspected = false;
        QTimer::singleShot(20, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            QCOMPARE(client.hosts, QStringList{QStringLiteral("192.168.43.8")});
            QCOMPARE(client.ports, QList<quint16>{8765});
            QVERIFY(!field<QWidget>(dialog, "onlineServerSettings")->isVisible());
            QCOMPARE(field<QLineEdit>(dialog, "onlinePassword")->echoMode(), QLineEdit::Password);
            QCOMPARE(field<QLineEdit>(dialog, "onlinePassword")->inputMethodHints(),
                     Qt::InputMethodHints(Qt::ImhSensitiveData | Qt::ImhNoAutoUppercase));
            QCOMPARE(field<QLabel>(dialog, "onlineStatus")->textFormat(), Qt::PlainText);
            inspected = true;
            enterCredentials(dialog);
        });
        QVERIFY(online_ui::signIn(client, user, nullptr, settingsDirectory.filePath("missing.ini")));
        QVERIFY(inspected);
        QCOMPARE(user.userId, qint64(42));
        QCOMPARE(user.username, QStringLiteral("alice"));
        QCOMPARE(client.passwords, QStringList{QStringLiteral("private-password")});
        const auto disconnects = client.disconnects;
        QCoreApplication::processEvents();
        QCOMPARE(client.disconnects, disconnects);
        QSettings saved;
        QCOMPARE(saved.value("online/host").toString(), QStringLiteral("192.168.43.8"));
        QCOMPARE(saved.value("online/lastUsername").toString(), QStringLiteral("alice"));
        for (const auto& key : saved.allKeys()) {
            QVERIFY(!key.contains("password", Qt::CaseInsensitive));
            QVERIFY(saved.value(key).toString() != QStringLiteral("private-password"));
        }
    }

    void failedConnectionRemainsResponsiveAndCanRetryEditedEndpoint()
    {
        StartupClient client;
        client.connectStatus = {ErrorCode::NotConnected, QStringLiteral("server unavailable")};
        UserInfo user;
        bool retried = false;
        QTimer::singleShot(20, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            QVERIFY(field<QWidget>(dialog, "onlineServerSettings")->isVisible());
            QVERIFY(field<QLabel>(dialog, "onlineStatus")->text().contains("server unavailable"));
            QVERIFY(!field<QPushButton>(dialog, "onlineLogin")->isEnabled());
            field<QLineEdit>(dialog, "onlineHost")->setText("new-server.test");
            field<QSpinBox>(dialog, "onlinePort")->setValue(9999);
            client.connectStatus = {};
            field<QPushButton>(dialog, "onlineConnect")->click();
            QCOMPARE(client.hosts.last(), QStringLiteral("new-server.test"));
            QCOMPARE(client.ports.last(), quint16(9999));
            retried = true;
            enterCredentials(dialog);
        });
        QVERIFY(online_ui::signIn(client, user));
        QVERIFY(retried);
        QCOMPARE(online_ui::loadConnectionSettings().host, QStringLiteral("new-server.test"));
    }

    void retriesIgnoreEarlierConnectionAndLoginCallbacksAndClearPassword()
    {
        StartupClient client;
        client.deferConnect = true;
        client.deferLogin = true;
        UserInfo user;
        bool inspected = false;
        QTimer::singleShot(20, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            QCOMPARE(client.connections.size(), 1);
            field<QPushButton>(dialog, "onlineChangeServer")->click();
            field<QPushButton>(dialog, "onlineConnect")->click();
            QCOMPARE(client.connections.size(), 2);
            client.connections[0]({});
            QVERIFY(!field<QPushButton>(dialog, "onlineLogin")->isEnabled());
            client.connections[1]({});
            enterCredentials(dialog);
            QCOMPARE(client.logins.size(), 1);
            QVERIFY(field<QLineEdit>(dialog, "onlinePassword")->text().isEmpty());
            field<QPushButton>(dialog, "onlineConnect")->click();
            client.logins[0]({{}, UserInfo{99, QStringLiteral("stale")}});
            QCOMPARE(dialog->result(), int(QDialog::Rejected));
            QVERIFY(dialog->isVisible());
            client.connections[2]({});
            enterCredentials(dialog);
            inspected = true;
            client.logins[1]({{}, UserInfo{42, QStringLiteral("alice")}});
        });
        QVERIFY(online_ui::signIn(client, user));
        QVERIFY(inspected);
        QCOMPARE(user.username, QStringLiteral("alice"));
    }

    void failedAuthenticationAllowsAnotherPasswordWithoutSavingSettings()
    {
        StartupClient client;
        client.deferLogin = true;
        UserInfo user;
        QTimer::singleShot(20, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            enterCredentials(dialog);
            client.logins[0]({{ErrorCode::AuthenticationFailed, QStringLiteral("wrong password")}, {}});
            QVERIFY(field<QLineEdit>(dialog, "onlinePassword")->text().isEmpty());
            QVERIFY(field<QLineEdit>(dialog, "onlinePassword")->isEnabled());
            QVERIFY(field<QPushButton>(dialog, "onlineLogin")->isEnabled());
            QVERIFY(field<QLabel>(dialog, "onlineStatus")->text().contains("wrong password"));
            QVERIFY(QSettings().allKeys().isEmpty());
            QCOMPARE(client.hosts.size(), 1);
            enterCredentials(dialog);
            client.logins[1]({{}, UserInfo{42, QStringLiteral("alice")}});
        });
        QVERIFY(online_ui::signIn(client, user));
        QCOMPARE(user.username, QStringLiteral("alice"));
    }

    void rejectionDisconnectsAndLateCallbacksAreSafe()
    {
        StartupClient client;
        client.deferConnect = true;
        UserInfo user;
        QTimer::singleShot(20, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            field<QLineEdit>(dialog, "onlinePassword")->setText("discard-me");
            dialog->reject();
        });
        QVERIFY(!online_ui::signIn(client, user));
        QVERIFY(client.disconnects > 0);
        QCOMPARE(client.connections.size(), 1);
        client.connections[0]({});
        QVERIFY(QSettings().allKeys().isEmpty());
        QCOMPARE(user.userId, qint64(0));
    }

private:
    QTemporaryDir settingsDirectory;
    QTimer watchdog;
};

QTEST_MAIN(OnlineStartupTests)
#include "online_startup_tests.moc"
