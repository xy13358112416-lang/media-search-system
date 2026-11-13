#include "personalrecommendation_service.h"
#include "account_history_service.h"
#include "mediarecommendation.h"
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

using namespace recommendation_contract;

class RecommendationIntegrationTests : public QObject {
    Q_OBJECT
private slots:
    void sqliteHistoryDrivesRecommendationsAcrossLoginAndRestart() {
        QTemporaryDir dir;
        const auto csv = dir.filePath("records.csv");
        const auto database = dir.filePath("accounts.sqlite");
        QVERIFY(QFile::copy(QString::fromUtf8(SAMPLE_CSV), csv));
        QFile original(csv); QVERIFY(original.open(QIODevice::ReadOnly));
        const auto originalBytes = original.readAll(); original.close();
        qint64 adminId = 0, userId = 0;
        {
            AccountHistoryServiceImpl accounts(csv);
            QVERIFY(accounts.initializeDatabase(database).ok());
            QVERIFY(accounts.createInitialAdmin("admin", "secret").ok());
            QVERIFY(accounts.registerUser("reader", "secret").ok());
            const auto admin = accounts.login("admin", "secret");
            QVERIFY(admin.status.ok() && admin.value); adminId = admin.value->userId;
            QVERIFY(accounts.recordMediaView(adminId, "M000001").ok());
            const auto user = accounts.login("reader", "secret");
            QVERIFY(user.status.ok() && user.value); userId = user.value->userId;
            QVERIFY(accounts.recordMediaView(userId, "M000004").ok());
        }
        AccountHistoryServiceImpl accounts(csv);
        QVERIFY(accounts.initializeDatabase(database).ok());
        MediaLibrary media(csv, {}); QVERIFY(media.load().success);
        PersonalRecommendationServiceImpl service(accounts, [&] { return media.records(); });
        QVERIFY(accounts.login("reader", "secret").status.ok());
        auto reader = service.recommend({userId});
        QVERIFY(reader.success);
        QCOMPARE(reader.mediaIds, recommendMedia(media.records(), {QStringLiteral("科幻"), "MP4", 5}));
        QVERIFY(accounts.login("admin", "secret").status.ok());
        auto admin = service.recommend({adminId});
        QVERIFY(admin.success);
        QCOMPARE(admin.mediaIds, recommendMedia(media.records(), {QStringLiteral("纪录片"), "MP4", 5}));
        QVERIFY(admin.mediaIds != reader.mediaIds);
        QVERIFY(!service.recommend({userId}).success);
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), originalBytes); // Clicks/recommendation never rewrite the media CSV.
        original.close();

        // Delete the item that contributed the administrator's only history entry.
        QVERIFY(accounts.removeMedia("M000001").status.ok());
        QVERIFY(media.load().success); // This sync remains the frontend/assembly owner's responsibility.
        auto afterDelete = service.recommend({adminId});
        QVERIFY(afterDelete.success);
        QCOMPARE(afterDelete.mediaIds, recommendMedia(media.records(), {}));
        QVERIFY(!afterDelete.mediaIds.contains("M000001"));

        // A real database fault must not be treated as an empty user history.
        const QString connection = QStringLiteral("recommendation-fault-injection");
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(database); QVERIFY(db.open());
            QSqlQuery query(db); QVERIFY(query.exec("DROP TABLE media_history"));
        }
        QSqlDatabase::removeDatabase(connection);
        auto failed = service.recommend({adminId});
        QVERIFY(!failed.success); QVERIFY(!failed.error.isEmpty()); QVERIFY(failed.mediaIds.isEmpty());
        accounts.logout(); QVERIFY(!service.recommend({adminId}).success);
    }
};
QTEST_GUILESS_MAIN(RecommendationIntegrationTests)
#include "personalrecommendation_integration_tests.moc"
