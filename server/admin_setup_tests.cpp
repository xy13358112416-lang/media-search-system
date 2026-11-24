#include "admin_setup_dialog.h"
#include "warehouse_server.h"
#include "password_policy.h"
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

class AdminSetupTests : public QObject {
    Q_OBJECT
    static void fill(AdminSetupDialog& dialog, const QString& name, const QString& first,
                     const QString& second)
    {
        dialog.findChild<QLineEdit*>("setupUsername")->setText(name);
        dialog.findChild<QLineEdit*>("setupPassword")->setText(first);
        dialog.findChild<QLineEdit*>("setupConfirmation")->setText(second);
        dialog.findChild<QPushButton*>("createAdministrator")->click();
    }
private slots:
    void passwordLengthCountsUnicodeCharacters()
    {
        for (const auto& password : {QStringLiteral("12345"), QStringLiteral("中文"),
                 QStringLiteral("一二三四五"), QString::fromUtf8("😀😀😀")})
            QVERIFY(online_password::validationError(password).contains(QStringLiteral("6 个字符")));
        for (const auto& password : {QStringLiteral("123456"), QStringLiteral("一二三四五六"),
                 QString::fromUtf8("😀😀😀😀😀😀"), QStringLiteral("中A文12好")})
            QVERIFY(online_password::validationError(password).isEmpty());
        QVERIFY(!online_password::validationError(QString(1025, 'a')).isEmpty());
        QVERIFY(!online_password::validationError(QString(342, QChar(0x4e2d))).isEmpty());
        QTemporaryDir directory;
        AdminSetupDialog dialog(directory.filePath("unicode.sqlite"));
        fill(dialog, "operator", QStringLiteral("一二三四五"), QStringLiteral("一二三四五"));
        QVERIFY(dialog.findChild<QLabel*>("setupStatus")->text().contains(QStringLiteral("6 个字符")));
        fill(dialog, "operator", QStringLiteral("一二三四五六"), QStringLiteral("一二三四五六"));
        QVERIFY(dialog.findChild<QLabel*>("setupStatus")->text().contains(QStringLiteral("创建成功")));
    }
    void validatesConfirmationAndLocksAfterSuccess()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("accounts.sqlite");
        AdminSetupDialog dialog(path);
        auto* create = dialog.findChild<QPushButton*>("createAdministrator");
        auto* password = dialog.findChild<QLineEdit*>("setupPassword");
        auto* status = dialog.findChild<QLabel*>("setupStatus");
        QVERIFY(create->isEnabled());
        QCOMPARE(password->echoMode(), QLineEdit::Password);
        QCOMPARE(dialog.findChild<QLineEdit*>("setupConfirmation")->echoMode(), QLineEdit::Password);
        fill(dialog, "first-admin", "test-pass-123", "different-pass");
        QVERIFY(status->text().contains(QStringLiteral("不一致")));
        fill(dialog, "first-admin", "123", "123");
        QVERIFY(status->text().contains(QStringLiteral("密码长度")));
        QVERIFY(password->text().isEmpty());
        fill(dialog, "first-admin", "test-pass-123", "test-pass-123");
        QVERIFY(status->text().contains(QStringLiteral("创建成功")));
        QVERIFY(password->text().isEmpty());
        QVERIFY(!create->isEnabled());
        AdminSetupDialog reopened(path);
        QVERIFY(!reopened.findChild<QPushButton*>("createAdministrator")->isEnabled());
        QVERIFY(reopened.findChild<QLabel*>("setupStatus")->text().contains(QStringLiteral("已有管理员")));
    }
    void doesNotPromoteExistingUserAndRechecksConcurrentInitialization()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("accounts.sqlite");
        WarehouseServer server;
        QString error;
        QVERIFY(server.openDatabase(path, error));
        QVERIFY(server.addUser("reader", "test-pass-123", error));
        AdminSetupDialog first(path), second(path);
        fill(first, "reader", "test-pass-123", "test-pass-123");
        QVERIFY(first.findChild<QPushButton*>("createAdministrator")->isEnabled());
        QCOMPARE(server.hasAdministrator(error), std::optional<bool>(false));
        fill(first, "operator", "test-pass-123", "test-pass-123");
        fill(second, "second-operator", "test-pass-123", "test-pass-123");
        QVERIFY(second.findChild<QLabel*>("setupStatus")->text().contains(QStringLiteral("已有管理员")));
        QVERIFY(!second.findChild<QPushButton*>("createAdministrator")->isEnabled());
        QCOMPARE(server.hasAdministrator(error), std::optional<bool>(true));
    }
    void databaseFailureDoesNotEnableCreation()
    {
        QTemporaryDir directory;
        AdminSetupDialog dialog(directory.filePath("missing/invalid.sqlite"));
        QVERIFY(!dialog.findChild<QPushButton*>("createAdministrator")->isEnabled());
        QVERIFY(!dialog.findChild<QLabel*>("setupStatus")->text().isEmpty());
    }
    void auditFailureRollsBackFirstAdministrator()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("accounts.sqlite");
        WarehouseServer server;
        QString error;
        QVERIFY(server.openDatabase(path, error));
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", "setup-test-audit");
            db.setDatabaseName(path);
            QVERIFY(db.open());
            {
                QSqlQuery query(db);
                QVERIFY(query.exec("CREATE TRIGGER reject_admin_audit BEFORE INSERT ON admin_audit "
                                   "BEGIN SELECT RAISE(ABORT,'test'); END"));
            }
            db.close();
        }
        QSqlDatabase::removeDatabase("setup-test-audit");
        QVERIFY(!server.initializeAdministrator("operator", "test-pass-123", error));
        QCOMPARE(server.hasAdministrator(error), std::optional<bool>(false));
        // The original username remains available because the account insertion rolled back.
        QVERIFY(server.addUser("operator", "test-pass-123", error));
    }
};

QTEST_MAIN(AdminSetupTests)
#include "admin_setup_tests.moc"
