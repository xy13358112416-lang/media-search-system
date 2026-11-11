#include "medialibrary.h"
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class BusinessTests : public QObject {
    Q_OBJECT
private slots:
    void sampleDeleteAndRestart();
    void specialCharactersAndFailedUpdates();
    void directoryAndPassword();
    void legacyAndInvalidCsv();
    void failureKeepsMemoryAndIndex();
    void lockedFileRejectsSave();
    void externalChangeRequiresReload();
    void rejectUnserializableDates();
};

void BusinessTests::sampleDeleteAndRestart() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("records.csv");
    QVERIFY(QFile::copy(QString::fromUtf8(SAMPLE_CSV), path));
    MediaLibrary library(path, QStringLiteral("course-pass"));
    QVERIFY2(library.load().success, "sample should load");
    QCOMPARE(library.records().size(), 30);
    SearchQuery q{QString::fromUtf8("林川"), QString::fromUtf8("陈明"), "MP4"};
    QCOMPARE(library.search(q), (QStringList{"M000001", "M000002"}));
    QVERIFY(library.remove("M000001").success);
    QCOMPARE(library.search(q), (QStringList{"M000002"}));
    MediaLibrary restarted(path, QStringLiteral("course-pass"));
    QVERIFY(restarted.load().success);
    QCOMPARE(restarted.search(q), (QStringList{"M000002"}));
}

void BusinessTests::specialCharactersAndFailedUpdates() {
    QTemporaryDir dir;
    const QString path = dir.filePath("records.csv");
    QVERIFY(QFile::copy(QString::fromUtf8(SAMPLE_CSV), path));
    MediaLibrary library(path, "pass");
    QVERIFY(library.load().success);
    MediaRecord record = library.records().first();
    record.id = "SPECIAL";
    record.fileName = QString::fromUtf8("名称,\"引号\"\n第二行.mp4");
    record.actors = QStringList{QString::fromUtf8("甲,乙"), QString::fromUtf8("丙\"丁")};
    record.awards = QStringList{};
    QVERIFY(library.add(record).success);
    QVERIFY(!library.add(record).success);
    QVERIFY(!library.remove("missing").success);
    MediaLibrary restarted(path, "pass");
    QVERIFY(restarted.load().success);
    MediaRecord loaded;
    QVERIFY(restarted.find("SPECIAL", loaded));
    QCOMPARE(loaded.fileName, record.fileName);
    QCOMPARE(loaded.actors, record.actors);
    QCOMPARE(loaded.awards, record.awards);
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("broken,header\n"); file.close();
    QVERIFY(!library.load().success);
    QVERIFY(library.find("SPECIAL", loaded));
}

void BusinessTests::directoryAndPassword() {
    QTemporaryDir dir;
    const QString path = dir.filePath("records.csv");
    QVERIFY(QFile::copy(QString::fromUtf8(SAMPLE_CSV), path));
    MediaLibrary library(path, QStringLiteral("secret"));
    QVERIFY(library.load().success);
    QVERIFY(!library.authenticate(QStringLiteral("wrong")));
    QVERIFY(library.authenticate(QStringLiteral("secret")));
    auto records = library.records("MP4");
    QVERIFY(!records.isEmpty());
    for (qsizetype i = 1; i < records.size(); ++i)
        QVERIFY(records[i-1].releaseDate >= records[i].releaseDate);
    QVERIFY(library.fileTypes().contains("MP4"));
    MediaRecord found;
    QVERIFY(library.find("M000001", found));
    QCOMPARE(found.id, QString("M000001"));
    QVERIFY(!library.find("NOT_FOUND", found));
}

void BusinessTests::legacyAndInvalidCsv() {
    QTemporaryDir dir;
    const auto path = dir.filePath("legacy.csv");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("id,file_name,file_type,file_size,director,actors,view_count,release_date,awards\n"
               "ID1,test.mp4,MP4,123,director,\"[\"\"actor\"\"]\",1,2024-01-01,[]\n");
    file.close();
    CsvMediaRepository repository(path);
    QList<MediaRecord> data;
    QVERIFY(repository.load(data).success);
    QCOMPARE(data.size(), 1);
    QCOMPARE(data.first().actors, QStringList{"actor"});
    QVERIFY(data.first().category.isEmpty());
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("id,file_name,file_type,file_size,director,actors,view_count,release_date,awards\n"
               "ID1,test.mp4,MP4,123,director,not-json,1,2024-01-01,[]\n");
    file.close();
    QVERIFY(!repository.load(data).success);
    QCOMPARE(data.size(), 1);
    QCOMPARE(data.first().id, QString("ID1"));
}

void BusinessTests::failureKeepsMemoryAndIndex() {
    QTemporaryDir dir;
    const auto path = dir.filePath("records.csv");
    QVERIFY(QFile::copy(QString::fromUtf8(SAMPLE_CSV), path));
    MediaLibrary library(path, "pass");
    QVERIFY(library.load().success);
    SearchQuery q{QString::fromUtf8("林川"), QString::fromUtf8("陈明"), "MP4"};
    QVERIFY(QFile::remove(path));
    const auto result = library.remove("M000001");
    QVERIFY(!result.success);
    QVERIFY(!result.error.isEmpty());
    QCOMPARE(library.search(q), (QStringList{"M000001", "M000002"}));
    MediaRecord unchanged;
    QVERIFY(library.find("M000001", unchanged));
}

void BusinessTests::lockedFileRejectsSave() {
#ifdef Q_OS_WIN
    QTemporaryDir dir;
    const auto path = dir.filePath("records.csv");
    QVERIFY(QFile::copy(QString::fromUtf8(SAMPLE_CSV), path));
    MediaLibrary library(path, "pass");
    QVERIFY(library.load().success);
    QFile locked(path);
    QVERIFY(locked.open(QIODevice::ReadOnly));
    const auto result = library.remove("M000001");
    QVERIFY(!result.success);
    QVERIFY(!result.error.isEmpty());
    locked.close();
    MediaRecord found;
    QVERIFY(library.find("M000001", found));
    MediaLibrary restarted(path, "pass");
    QVERIFY(restarted.load().success);
    QVERIFY(restarted.find("M000001", found));
#else
    QSKIP("Windows-specific file replacement lock test");
#endif
}
void BusinessTests::externalChangeRequiresReload() {
    QTemporaryDir dir;
    const auto path = dir.filePath("records.csv");
    QVERIFY(QFile::copy(QString::fromUtf8(SAMPLE_CSV), path));
    MediaLibrary first(path, "pass"), second(path, "pass");
    QVERIFY(first.load().success);
    QVERIFY(second.load().success);
    QVERIFY(first.remove("M000001").success);
    auto result = second.remove("M000002");
    QVERIFY(!result.success);
    QVERIFY(!result.error.isEmpty());
    QVERIFY(second.load().success);
    MediaRecord record;
    QVERIFY(second.find("M000002", record));
    QVERIFY(!second.find("M000001", record));
}
void BusinessTests::rejectUnserializableDates() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = dir.filePath("records.csv");
    QVERIFY(QFile::copy(QString::fromUtf8(SAMPLE_CSV), path));
    MediaLibrary library(path, "pass");
    QVERIFY(library.load().success);
    QFile original(path);
    QVERIFY(original.open(QIODevice::ReadOnly));
    const QByteArray before = original.readAll();
    original.close();
    MediaRecord record = library.records().first();
    record.id = "DATE-INVALID";
    record.releaseDate = QDate(10000, 1, 1);
    QVERIFY(record.releaseDate.isValid());
    const auto result = library.add(record);
    QVERIFY(!result.success);
    QVERIFY(!result.error.isEmpty());
    QVERIFY(original.open(QIODevice::ReadOnly));
    QCOMPARE(original.readAll(), before);
    original.close();
    MediaLibrary restarted(path, "pass");
    QVERIFY(restarted.load().success);
    SearchQuery q{QString::fromUtf8("林川"), QString::fromUtf8("陈明"), "MP4"};
    QCOMPARE(restarted.search(q), (QStringList{"M000001", "M000002"}));
    MediaRecord found;
    QVERIFY(!restarted.find("DATE-INVALID", found));
    for (const auto& invalid : {QDate(-1, 1, 1), QDate(10000, 12, 31)}) {
        record.releaseDate = invalid;
        QVERIFY(!library.add(record).success);
    }
    record.id = "DATE-MIN";
    record.releaseDate = QDate(1, 1, 1);
    QVERIFY(library.add(record).success);
    record.id = "DATE-MAX";
    record.releaseDate = QDate(9999, 12, 31);
    QVERIFY(library.add(record).success);
    MediaLibrary boundaries(path, "pass");
    QVERIFY(boundaries.load().success);
    QVERIFY(boundaries.find("DATE-MIN", found));
    QCOMPARE(found.releaseDate, QDate(1, 1, 1));
    QVERIFY(boundaries.find("DATE-MAX", found));
    QCOMPARE(found.releaseDate, QDate(9999, 12, 31));
}
QTEST_GUILESS_MAIN(BusinessTests)
#include "tests.moc"
