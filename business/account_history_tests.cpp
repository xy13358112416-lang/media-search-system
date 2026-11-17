#include "account_history_service.h"
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QSemaphore>
#include <QtTest>
#include <thread>

using namespace account_contract;
class AccountHistoryTests : public QObject {
    Q_OBJECT
private slots:
    void accountLifecycleAndHashes();
    void permissionsAndHistorySurviveRestart();
    void databaseFailuresAreNotEmptyHistory();
    void adminCreationAndSchemaFailures();
    void mediaChangesAreObserved();
    void failedCsvWriteReportsNotPersisted();
    void existingUnversionedTablesAreNotModified();
    void failedReinitializationClosesOldSession();
    void concurrentInitialAdminIsUnique();
    void rejectVersionedDatabaseWithoutConstraints();
    void sharedMediaLibraryStaysSynchronized();
};

void AccountHistoryTests::sharedMediaLibraryStaysSynchronized() {
    QTemporaryDir dir;
    const auto csv = dir.filePath("records.csv");
    const auto database = dir.filePath("accounts.sqlite");
    QVERIFY(QFile::copy(QString::fromUtf8(SAMPLE_CSV), csv));
    MediaLibrary library(csv, {});
    QVERIFY(library.load().success);
    AccountHistoryServiceImpl accounts(library);
    QVERIFY(accounts.initializeDatabase(database).ok());
    QVERIFY(accounts.createInitialAdmin("admin", "secret").ok());
    QVERIFY(accounts.registerUser("reader", "secret").ok());
    const auto admin = accounts.login("admin", "secret");
    QVERIFY(admin.status.ok());

    MediaRecord record;
    record.fileName = "shared.mp4";
    record.fileType = "MP4";
    record.director = QStringLiteral("共享导演");
    record.releaseDate = QDate(2024, 1, 1);
    const auto added = accounts.addMedia(record);
    QVERIFY(added.status.ok());
    QVERIFY(added.persisted);
    MediaRecord found;
    QVERIFY(library.find(added.mediaId, found));
    QVERIFY(library.search({QStringLiteral("共享导演"), {}, "MP4"}).contains(added.mediaId));
    QVERIFY(accounts.recordMediaView(admin.value->userId, added.mediaId).ok());

    QVERIFY(accounts.login("reader", "secret").status.ok());
    QCOMPARE(accounts.removeMedia(added.mediaId).status.code, ErrorCode::PermissionDenied);
    QVERIFY(library.find(added.mediaId, found));
    QVERIFY(accounts.login("admin", "secret").status.ok());
    const auto removed = accounts.removeMedia(added.mediaId);
    QVERIFY(removed.status.ok());
    QVERIFY(removed.persisted);
    QVERIFY(!library.find(added.mediaId, found));
    QVERIFY(!library.search({QStringLiteral("共享导演"), {}, "MP4"}).contains(added.mediaId));

    MediaLibrary restarted(csv, {});
    QVERIFY(restarted.load().success);
    QVERIFY(!restarted.find(added.mediaId, found));
    QVERIFY(accounts.initializeDatabase(database).ok());
    QVERIFY(accounts.login("admin", "secret").status.ok());
    QVERIFY(accounts.recordMediaView(admin.value->userId, "M000001").ok());
}

void AccountHistoryTests::accountLifecycleAndHashes() {
    QTemporaryDir dir;
    const QString dbPath = dir.filePath("users.sqlite");
    AccountHistoryServiceImpl service;
    QVERIFY(service.initializeDatabase(dbPath).ok());
    QVERIFY(service.needsInitialAdmin().value.value());
    QCOMPARE(service.registerUser("early", "pass").code, ErrorCode::InitialAdminRequired);
    QVERIFY(service.createInitialAdmin(QString::fromUtf8(" 管理员 "), QString::fromUtf8("中文Pass")).ok());
    QVERIFY(!service.needsInitialAdmin().value.value());
    QCOMPARE(service.createInitialAdmin("other", "pw").code, ErrorCode::InitialAdminAlreadyExists);
    QVERIFY(service.registerUser(QStringLiteral("Alice"), "secret").ok());
    QCOMPARE(service.registerUser(QStringLiteral(" alice "), "different").code, ErrorCode::UsernameTaken);
    QVERIFY(service.registerUser(QString::fromUtf8("é"), "pass").ok());
    QCOMPARE(service.registerUser(QString::fromUtf8("e\u0301"), "pass").code, ErrorCode::UsernameTaken);
    QVERIFY(!service.login("alice", "wrong").status.ok());
    QVERIFY(!service.currentUser().has_value());
    auto admin = service.login(QString::fromUtf8("管理员"), QString::fromUtf8("中文Pass"));
    QVERIFY(admin.status.ok());
    QCOMPARE(admin.value->role, UserRole::Admin);
    service.logout();
    QVERIFY(!service.currentUser().has_value());
    auto user = service.login("ALICE", "secret");
    QVERIFY(user.status.ok());
    QCOMPARE(user.value->role, UserRole::User);
    const QString connection = "inspect-hashes";
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
        db.setDatabaseName(dbPath); QVERIFY(db.open());
        QSqlQuery q(db); QVERIFY(q.exec("SELECT password_hash FROM users"));
        int count = 0;
        while (q.next()) { const QString value = q.value(0).toString(); QVERIFY(value.startsWith("$argon2id$")); QVERIFY(!value.contains("secret")); ++count; }
        QCOMPARE(count, 3);
        db.close();
    }
    QSqlDatabase::removeDatabase(connection);
}

void AccountHistoryTests::permissionsAndHistorySurviveRestart() {
    QTemporaryDir dir;
    const auto csv = dir.filePath("records.csv");
    const auto dbPath = dir.filePath("users.sqlite");
    QVERIFY(QFile::copy(QString::fromUtf8(SAMPLE_CSV), csv));
    qint64 adminId = 0, userId = 0;
    {
        AccountHistoryServiceImpl service(csv);
        QVERIFY(service.initializeDatabase(dbPath).ok());
        QVERIFY(service.createInitialAdmin("admin", "pass").ok());
        QVERIFY(service.registerUser("bob", "pass").ok());
        auto admin = service.login("admin", "pass"); QVERIFY(admin.status.ok()); adminId = admin.value->userId;
        QVERIFY(service.recordMediaView(adminId, "M000001").ok());
        QVERIFY(service.recordMediaView(adminId, "M000001").ok());
        auto user = service.login("bob", "pass"); QVERIFY(user.status.ok()); userId = user.value->userId;
        QCOMPARE(service.recordMediaView(adminId, "M000001").code, ErrorCode::PermissionDenied);
        QCOMPARE(service.loadMediaHistory(adminId).status.code, ErrorCode::PermissionDenied);
        QVERIFY(service.recordMediaView(userId, "M000002").ok());
        QCOMPARE(service.removeMedia("M000001").status.code, ErrorCode::PermissionDenied);
        MediaRecord forbidden; forbidden.fileName = "test.mp4"; forbidden.fileType = "MP4"; forbidden.releaseDate = QDate(2024, 1, 1);
        QCOMPARE(service.addMedia(forbidden).status.code, ErrorCode::PermissionDenied);
        QCOMPARE(service.recordMediaView(userId, "DOES-NOT-EXIST").code, ErrorCode::MediaNotFound);
        service.logout();
        QCOMPARE(service.removeMedia("M000001").status.code, ErrorCode::NotAuthenticated);
    }
    {
        AccountHistoryServiceImpl restarted(csv);
        QVERIFY(restarted.initializeDatabase(dbPath).ok());
        auto admin = restarted.login("admin", "pass"); QVERIFY(admin.status.ok());
        auto history = restarted.loadMediaHistory(adminId); QVERIFY(history.status.ok());
        QCOMPARE(history.value->size(), 1);
        QCOMPARE(history.value->first().viewCount, qint64(2));
        QVERIFY(history.value->first().lastViewedAt > 0);
        const auto removed = restarted.removeMedia("M000001");
        QVERIFY(removed.status.ok()); QVERIFY(removed.persisted);
        auto user = restarted.login("bob", "pass"); QVERIFY(user.status.ok());
        auto userHistory = restarted.loadMediaHistory(userId); QVERIFY(userHistory.status.ok());
        QCOMPARE(userHistory.value->size(), 1);
        QCOMPARE(userHistory.value->first().mediaId, QString("M000002"));
    }
}

void AccountHistoryTests::databaseFailuresAreNotEmptyHistory() {
    AccountHistoryServiceImpl service;
    QVERIFY(AccountHistoryServiceImpl::defaultDatabasePath().endsWith("accounts.sqlite"));
    QVERIFY(!service.needsInitialAdmin().status.ok());
    QVERIFY(!service.loadMediaHistory(1).status.ok());
    QVERIFY(!service.initializeDatabase(QString()).ok());
    QTemporaryDir dir;
    const auto path = dir.filePath("failure.sqlite");
    QVERIFY(service.initializeDatabase(path).ok());
    QVERIFY(service.createInitialAdmin("admin", "pass").ok());
    auto identity = service.login("admin", "pass"); QVERIFY(identity.status.ok());
    const QString connection = "simulate-history-failure";
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
        db.setDatabaseName(path); QVERIFY(db.open());
        QSqlQuery q(db); QVERIFY(q.exec("DROP TABLE media_history")); db.close();
    }
    QSqlDatabase::removeDatabase(connection);
    const auto result = service.loadMediaHistory(identity.value->userId);
    QCOMPARE(result.status.code, ErrorCode::DatabaseError);
    QVERIFY(!result.value.has_value());
}

void AccountHistoryTests::adminCreationAndSchemaFailures() {
    QTemporaryDir dir;
    const QString dbPath = dir.filePath("users.sqlite");
    AccountHistoryServiceImpl first, second;
    QVERIFY(first.initializeDatabase(dbPath).ok());
    QVERIFY(second.initializeDatabase(dbPath).ok());
    QVERIFY(first.createInitialAdmin("first", "same-password").ok());
    QCOMPARE(second.createInitialAdmin("second", "same-password").code, ErrorCode::InitialAdminAlreadyExists);
    QVERIFY(second.registerUser("second", "same-password").ok());
    const QString connection = "inspect-salts";
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
        db.setDatabaseName(dbPath); QVERIFY(db.open());
        QSqlQuery q(db); QVERIFY(q.exec("SELECT password_hash FROM users ORDER BY user_id"));
        QVERIFY(q.next()); const QString a = q.value(0).toString();
        QVERIFY(q.next()); const QString b = q.value(0).toString();
        QVERIFY(a != b); db.close();
    }
    QSqlDatabase::removeDatabase(connection);
}

void AccountHistoryTests::mediaChangesAreObserved() {
    QTemporaryDir dir;
    const auto path = dir.filePath("records.csv");
    QVERIFY(QFile::copy(QString::fromUtf8(SAMPLE_CSV), path));
    AccountHistoryServiceImpl service(path);
    QVERIFY(service.initializeDatabase(dir.filePath("users.sqlite")).ok());
    QVERIFY(service.createInitialAdmin("admin", "pass").ok());
    auto login = service.login("admin", "pass"); QVERIFY(login.status.ok());
    const auto userId = login.value->userId;
    MediaRecord invalid; invalid.fileName = "bad.exe"; invalid.fileType = "EXE"; invalid.releaseDate = QDate(2024, 1, 1);
    QCOMPARE(service.addMedia(invalid).status.code, ErrorCode::InvalidArgument);
    QCOMPARE(service.removeMedia(" ").status.code, ErrorCode::InvalidArgument);
    QCOMPARE(service.recordMediaView(userId, " ").code, ErrorCode::InvalidArgument);
    QVERIFY(service.recordMediaView(userId, "M000001").ok());
    MediaLibrary external(path, "unused"); QVERIFY(external.load().success);
    QVERIFY(external.remove("M000001").success);
    QCOMPARE(service.recordMediaView(userId, "M000001").code, ErrorCode::MediaNotFound);
    MediaRecord fresh; fresh.fileName = "new.mp4"; fresh.fileType = "MP4"; fresh.releaseDate = QDate(2024, 1, 1);
    const auto added = service.addMedia(fresh);
    QVERIFY(added.status.ok()); QVERIFY(added.persisted);
    QVERIFY(!added.mediaId.isEmpty());
    QVERIFY(service.recordMediaView(userId, added.mediaId).ok());
}

void AccountHistoryTests::failedCsvWriteReportsNotPersisted() {
#ifdef Q_OS_WIN
    QTemporaryDir dir;
    const auto path = dir.filePath("records.csv");
    QVERIFY(QFile::copy(QString::fromUtf8(SAMPLE_CSV), path));
    AccountHistoryServiceImpl service(path);
    QVERIFY(service.initializeDatabase(dir.filePath("users.sqlite")).ok());
    QVERIFY(service.createInitialAdmin("admin", "pass").ok());
    QVERIFY(service.login("admin", "pass").status.ok());
    QFile locked(path); QVERIFY(locked.open(QIODevice::ReadOnly));
    const auto result = service.removeMedia("M000001");
    QVERIFY(!result.status.ok()); QVERIFY(!result.persisted);
    locked.close();
    CsvMediaRepository repository(path);
    QList<MediaRecord> records; QVERIFY(repository.load(records).success);
    QCOMPARE(records.size(), 30);
#else
    QSKIP("Windows file replacement lock test");
#endif
}

void AccountHistoryTests::existingUnversionedTablesAreNotModified() {
    QTemporaryDir dir;
    const auto path = dir.filePath("legacy.sqlite");
    const QString connection = "legacy-schema";
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
        db.setDatabaseName(path); QVERIFY(db.open());
        QSqlQuery q(db); QVERIFY(q.exec("CREATE TABLE users(user_id INTEGER, custom_data TEXT)"));
        QVERIFY(q.exec("INSERT INTO users VALUES(12, 'keep')"));
        db.close();
    }
    QSqlDatabase::removeDatabase(connection);
    AccountHistoryServiceImpl service;
    QVERIFY(!service.initializeDatabase(path).ok());
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
        db.setDatabaseName(path); QVERIFY(db.open());
        QSqlQuery q(db); QVERIFY(q.exec("SELECT custom_data FROM users WHERE user_id=12"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), QString("keep"));
        QVERIFY(q.exec("PRAGMA user_version")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 0);
        db.close();
    }
    QSqlDatabase::removeDatabase(connection);
}

void AccountHistoryTests::failedReinitializationClosesOldSession() {
    QTemporaryDir dir;
    AccountHistoryServiceImpl service;
    QVERIFY(service.initializeDatabase(dir.filePath("db.sqlite")).ok());
    QVERIFY(service.createInitialAdmin("admin", "password").ok());
    QVERIFY(service.login("admin", "password").status.ok());
    QVERIFY(!service.initializeDatabase(QString()).ok());
    QVERIFY(!service.currentUser().has_value());
    QCOMPARE(service.registerUser("user", "password").code, ErrorCode::NotInitialized);
}

void AccountHistoryTests::concurrentInitialAdminIsUnique() {
    QTemporaryDir dir;
    const auto path = dir.filePath("concurrent.sqlite");
    AccountHistoryServiceImpl setup;
    QVERIFY(setup.initializeDatabase(path).ok());
    QSemaphore ready, go;
    Status firstInit, secondInit, firstResult, secondResult;
    auto worker = [&](const QString& name, Status& initialized, Status& created) {
        AccountHistoryServiceImpl service;
        initialized = service.initializeDatabase(path);
        ready.release(); go.acquire();
        if (initialized.ok()) created = service.createInitialAdmin(name, "password");
    };
    std::thread one(worker, QStringLiteral("admin-one"), std::ref(firstInit), std::ref(firstResult));
    std::thread two(worker, QStringLiteral("admin-two"), std::ref(secondInit), std::ref(secondResult));
    ready.acquire(2); go.release(2);
    one.join(); two.join();
    QVERIFY(firstInit.ok()); QVERIFY(secondInit.ok());
    QVERIFY(firstResult.ok() != secondResult.ok());
    QCOMPARE(firstResult.ok() ? secondResult.code : firstResult.code, ErrorCode::InitialAdminAlreadyExists);
}

void AccountHistoryTests::rejectVersionedDatabaseWithoutConstraints() {
    QTemporaryDir dir;
    const auto path = dir.filePath("bad-versioned.sqlite");
    const QString connection = "bad-versioned";
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
        db.setDatabaseName(path); QVERIFY(db.open());
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TABLE users(user_id INTEGER, username TEXT, username_key TEXT, password_hash TEXT, role TEXT, created_at INTEGER)"));
        QVERIFY(q.exec("CREATE TABLE media_history(user_id INTEGER, media_id TEXT, view_count INTEGER, last_viewed_at INTEGER)"));
        QVERIFY(q.exec("INSERT INTO users VALUES(5,'old','old','hash','admin',1)"));
        QVERIFY(q.exec("PRAGMA user_version=1")); db.close();
    }
    QSqlDatabase::removeDatabase(connection);
    AccountHistoryServiceImpl service;
    QVERIFY(!service.initializeDatabase(path).ok());
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
        db.setDatabaseName(path); QVERIFY(db.open());
        QSqlQuery q(db); QVERIFY(q.exec("SELECT username FROM users WHERE user_id=5"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), QString("old")); db.close();
    }
    QSqlDatabase::removeDatabase(connection);
}
QTEST_GUILESS_MAIN(AccountHistoryTests)
#include "account_history_tests.moc"
