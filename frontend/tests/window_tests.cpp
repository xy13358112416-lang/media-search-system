#include "window_test_support.h"
#include <QtTest>

using namespace UiTest;

class WindowTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        qApp->setStyle(QStringLiteral("Fusion"));
        qApp->setStyleSheet(libraryStyle());
    }

    void sameSizeRefreshHasBoundedLayoutWork_data() {
        QTest::addColumn<QString>("seed");
        QTest::newRow("30") << QString::fromUtf8(SAMPLE_CSV);
        QTest::newRow("530") << QString::fromUtf8(EXPANDED_CSV);
    }

    void sameSizeRefreshHasBoundedLayoutWork() {
        QFETCH(QString, seed);
        Fixture f(seed);
        const auto records = f.library.records();
        for (int tab : {1, 0, 1, 0}) {
            f.resetCounters();
            f.tabs->setCurrentIndex(tab);
            finishLayout(f.table);
            QCOMPARE(f.table->rowCount(), records.size());
            QVERIFY2(f.table->columnCalls <= 24, qPrintable(QStringLiteral("column scans: %1")
                                                           .arg(f.table->columnCalls)));
            QVERIFY(f.table->counter->hints <= 24 * (records.size() + 1));
            QCOMPARE(f.detail(QStringLiteral("编号")), records.first().id);
            QCOMPARE(f.table->horizontalHeader()->sectionResizeMode(0), QHeaderView::Stretch);
            for (int column = 1; column < 4; ++column)
                QCOMPARE(f.table->horizontalHeader()->sectionResizeMode(column), QHeaderView::ResizeToContents);
            QVERIFY(f.table->updatesEnabled());
        }
    }

    void searchesFiltersAndEmptyResultsStayCorrect() {
        Fixture f(QString::fromUtf8(SAMPLE_CSV));
        f.tabs->setCurrentIndex(1);
        auto* keyword = searchInput(f.window.get(), QStringLiteral("文件名关键词"));
        auto* search = button(f.window.get(), QStringLiteral("查询"));
        // Equal-size results must still replace the record and update its details.
        for (const auto& name : {QStringLiteral("晨光.mp3"), QStringLiteral("归途.wav")}) {
            keyword->setText(name);
            search->click();
            finishLayout(f.table);
            QCOMPARE(f.table->rowCount(), 1);
            QCOMPARE(f.table->item(0, 0)->text(), name);
            QCOMPARE(f.detail(QStringLiteral("文件名")), name);
            QCOMPARE(f.window->findChild<QLabel*>(QStringLiteral("resultCount"))->text(), QStringLiteral("命中 1 条"));
        }
        keyword->setText(QStringLiteral("不存在的回归作品"));
        search->click();
        finishLayout(f.table);
        QCOMPARE(f.table->rowCount(), 0);
        QCOMPARE(f.detail(QStringLiteral("编号")), QStringLiteral("—"));
        QVERIFY(!button(f.window.get(), QStringLiteral("删除"))->isEnabled());
        QVERIFY(f.table->updatesEnabled());
        QCOMPARE(f.table->horizontalHeader()->sectionResizeMode(0), QHeaderView::Stretch);
        QCOMPARE(f.table->horizontalHeader()->sectionResizeMode(1), QHeaderView::ResizeToContents);

        button(f.window.get(), QStringLiteral("清空条件"))->click();
        finishLayout(f.table);
        QCOMPARE(f.table->rowCount(), 30);
        f.tabs->setCurrentIndex(0);
        QComboBox* filter = nullptr;
        for (auto* candidate : f.window->findChildren<QComboBox*>())
            if (candidate->findText(QStringLiteral("全部类型")) >= 0)
                filter = candidate;
        QVERIFY(filter);
        filter->setCurrentIndex(filter->findData(QStringLiteral("WAV")));
        finishLayout(f.table);
        QCOMPARE(f.table->rowCount(), f.library.records(QStringLiteral("WAV")).size());
        for (int row = 0; row < f.table->rowCount(); ++row)
            QCOMPARE(f.table->item(row, 1)->text(), QStringLiteral("WAV"));
        f.tabs->setCurrentIndex(2);
        finishLayout(f.table);
        QCOMPARE(f.table->rowCount(), 5);
        QCOMPARE(f.table->horizontalHeaderItem(2)->text(), QStringLiteral("类别"));
        RecommendationOptions options;
        QCOMPARE(f.detail(QStringLiteral("编号")), recommendMedia(f.library.records(), options).first());
        f.tabs->setCurrentIndex(1);
        finishLayout(f.table);
        QCOMPARE(f.table->rowCount(), 30);
        QCOMPARE(f.table->horizontalHeaderItem(2)->text(), QStringLiteral("导演"));
    }

    void invalidTypeDoesNotCallBusinessOrRefresh() {
        Fixture f(QString::fromUtf8(SAMPLE_CSV));
        const auto csvBefore = readFile(f.csvPath);
        const auto sequenceBefore = readFile(f.csvPath + QStringLiteral(".id-sequence"));
        bool sawError = false;
        bool formKept = false;
        QString error;
        // Timers operate the application's real nested dialogs, without user input.
        QTimer::singleShot(0, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) return;
            f.fillAddForm(dialog, QStringLiteral("213"));
            QTimer::singleShot(0, [&] {
                auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                if (message) {
                    sawError = true;
                    error = message->text();
                    message->accept();
                }
            });
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
            formKept = dialog->isVisible() &&
                addInput(dialog, QStringLiteral("文件类型"))->text() == QStringLiteral("213") &&
                addInput(dialog, QStringLiteral("文件名"))->text() == QStringLiteral("回归新增.mp4");
            dialog->reject();
        });
        button(f.window.get(), QStringLiteral("新增"))->click();
        finishLayout(f.table);
        QVERIFY(sawError);
        QVERIFY(error.contains(QStringLiteral("不支持")));
        QVERIFY(formKept);
        QCOMPARE(f.library.addCalls, 0);
        QCOMPARE(f.library.loadCalls, 0);
        QCOMPARE(f.table->dataCalls, quint64(0));
        QCOMPARE(readFile(f.csvPath), csvBefore);
        QCOMPARE(readFile(f.csvPath + QStringLiteral(".id-sequence")), sequenceBefore);
    }

    void correctedInputCanSaveAndDelete() {
        Fixture f(QString::fromUtf8(SAMPLE_CSV));
        bool sawError = false;
        QTimer::singleShot(0, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) return;
            f.fillAddForm(dialog, QStringLiteral("213"));
            QTimer::singleShot(0, [&] {
                if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                    sawError = true;
                    message->accept();
                }
            });
            auto* save = dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save);
            save->click();
            addInput(dialog, QStringLiteral("文件类型"))->setText(QStringLiteral(" mp4 "));
            save->click();
            if (dialog->isVisible()) dialog->reject();
        });
        button(f.window.get(), QStringLiteral("新增"))->click();
        finishLayout(f.table);
        QVERIFY(sawError);
        QCOMPARE(f.library.addCalls, 1);
        QCOMPARE(f.library.loadCalls, 0);
        QCOMPARE(f.table->rowCount(), 31);
        const auto newId = f.detail(QStringLiteral("编号"));
        MediaRecord saved;
        QVERIFY(f.library.find(newId, saved));
        QCOMPARE(saved.fileType, QStringLiteral("MP4"));
        QVERIFY(f.library.search({QStringLiteral("回归导演"), {}, {}}).contains(newId));
        f.tabs->setCurrentIndex(1);
        finishLayout(f.table);
        QCOMPARE(f.table->rowCount(), 31);
        f.tabs->setCurrentIndex(0);
        for (int row = 0; row < f.table->rowCount(); ++row)
            if (f.table->item(row, 0)->text() == QStringLiteral("回归新增.mp4"))
                f.table->setCurrentCell(row, 0);
        QTimer::singleShot(0, [&] {
            if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                message->done(QMessageBox::Yes);
        });
        button(f.window.get(), QStringLiteral("删除"))->click();
        finishLayout(f.table);
        QCOMPARE(f.table->rowCount(), 30);
        QVERIFY(!f.library.find(newId, saved));
        QVERIFY(f.library.search({QStringLiteral("回归导演"), {}, {}}).isEmpty());
        f.tabs->setCurrentIndex(1);
        finishLayout(f.table);
        QCOMPARE(f.table->rowCount(), 30);
    }

    void externalChangeStillRecovers() {
        Fixture f(QString::fromUtf8(SAMPLE_CSV));
        const auto removed = f.library.records().first();
        CsvMediaRepository external(f.csvPath);
        QVERIFY(external.remove(removed.id).success);
        bool sawError = false;
        QTimer::singleShot(0, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) return;
            f.fillAddForm(dialog, QStringLiteral("MP4"));
            QTimer::singleShot(0, [&] {
                if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                    sawError = message->text().contains(QStringLiteral("外部修改"));
                    message->accept();
                }
            });
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
            dialog->reject();
        });
        button(f.window.get(), QStringLiteral("新增"))->click();
        finishLayout(f.table);
        QVERIFY(sawError);
        QCOMPARE(f.library.addCalls, 1);
        QCOMPARE(f.library.loadCalls, 1);
        QCOMPARE(f.table->rowCount(), 29);
        MediaRecord record;
        QVERIFY(!f.library.find(removed.id, record));
    }
};

QTEST_MAIN(WindowTests)
#include "window_tests.moc"
