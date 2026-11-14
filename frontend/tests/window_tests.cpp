#include "account_history_service.h"
#include "personalrecommendation_service.h"
#include "window_test_support.h"
#include <QTextEdit>
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

    void accountMutationsRefreshIndependentSnapshot() {
        struct Accounts final : account_contract::AccountHistoryService {
            MediaLibrary writer;
            explicit Accounts(const QString& path) : writer(path, {}) {}
            account_contract::Status initializeDatabase(const QString&) override { return {}; }
            account_contract::Result<bool> needsInitialAdmin() override { return {{}, false}; }
            account_contract::Status createInitialAdmin(const QString&, const QString&) override { return {}; }
            account_contract::Status registerUser(const QString&, const QString&) override { return {}; }
            account_contract::Result<account_contract::UserIdentity> login(const QString&, const QString&) override {
                return {{}, currentUser()};
            }
            std::optional<account_contract::UserIdentity> currentUser() const override {
                return account_contract::UserIdentity{1, QStringLiteral("admin"), account_contract::UserRole::Admin};
            }
            void logout() override {}
            account_contract::Status recordMediaView(qint64, const QString&) override { return {}; }
            account_contract::Result<QList<account_contract::MediaHistoryEntry>> loadMediaHistory(qint64) override {
                return {{}, QList<account_contract::MediaHistoryEntry>{}};
            }
            account_contract::MediaMutationResult addMedia(const MediaRecord& record) override {
                auto copy = record;
                const auto result = writer.addNew(copy);
                return {status(result), result.success, copy.id};
            }
            account_contract::MediaMutationResult removeMedia(const QString& id) override {
                const auto result = writer.remove(id);
                return {status(result), result.success, id};
            }
            static account_contract::Status status(const RepositoryResult& result) {
                return {result.success ? account_contract::ErrorCode::None
                                       : account_contract::ErrorCode::StorageError, result.error};
            }
        };

        Fixture f(QString::fromUtf8(SAMPLE_CSV));
        Accounts accounts(f.csvPath);
        QVERIFY(accounts.writer.load().success);
        f.window = std::make_unique<LibraryWindow>(f.library, &accounts);
        f.window->show();
        auto* table = f.window->findChild<QTableWidget*>();
        const SearchQuery query{QStringLiteral("回归导演"), {}, {}};

        QTimer::singleShot(0, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) return;
            f.fillAddForm(dialog, QStringLiteral("MP4"));
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
            if (dialog->isVisible()) dialog->reject();
        });
        button(f.window.get(), QStringLiteral("新增"))->click();
        QCOMPARE(table->rowCount(), 31);
        const auto ids = f.library.search(query);
        QCOMPARE(ids.size(), 1);
        QCOMPARE(f.detail(QStringLiteral("编号")), ids.first());
        QCOMPARE(table->item(table->currentRow(), 0)->text(), QStringLiteral("回归新增.mp4"));

        QTimer::singleShot(0, [] {
            if (auto* confirmation = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                confirmation->done(QMessageBox::Yes);
        });
        button(f.window.get(), QStringLiteral("删除"))->click();
        QCOMPARE(table->rowCount(), 30);
        QVERIFY(f.library.search(query).isEmpty());
        MediaRecord removed;
        QVERIFY(!f.library.find(ids.first(), removed));
    }

    void realAccountHistoryAndRecommendationFlow() {
        Fixture f(QString::fromUtf8(SAMPLE_CSV));
        AccountHistoryServiceImpl accounts(f.library);
        QVERIFY(accounts.initializeDatabase(f.directory.filePath("accounts.sqlite")).ok());
        QVERIFY(accounts.createInitialAdmin("admin", "secret").ok());
        QVERIFY(accounts.registerUser("reader", "secret").ok());
        const auto admin = accounts.login("admin", "secret");
        QVERIFY(admin.status.ok() && admin.value);
        recommendation_contract::PersonalRecommendationServiceImpl recommendations(
            accounts, [&] { return f.library.records(); });

        f.window = std::make_unique<LibraryWindow>(f.library, &accounts, &recommendations);
        f.window->show();
        auto* table = static_cast<ObservedTable*>(f.window->findChild<QTableWidget*>());
        auto* tabs = f.window->findChild<QTabBar*>();
        QVERIFY(table && tabs);
        finishLayout(table);
        QVERIFY(accounts.loadMediaHistory(admin.value->userId).value->isEmpty());
        const QString clickedId = f.detail(QStringLiteral("编号"));
        QTest::mouseClick(table->viewport(), Qt::LeftButton, Qt::NoModifier,
                          table->visualItemRect(table->item(0, 0)).center());
        auto history = accounts.loadMediaHistory(admin.value->userId);
        QVERIFY(history.status.ok() && history.value);
        QCOMPARE(history.value->size(), 1);
        QCOMPARE(history.value->first().mediaId, clickedId);
        tabs->setCurrentIndex(2);
        finishLayout(table);
        const auto result = recommendations.recommend({admin.value->userId});
        QVERIFY(result.success && !result.mediaIds.isEmpty());
        QCOMPARE(f.detail(QStringLiteral("编号")), result.mediaIds.first());
        QCOMPARE(accounts.loadMediaHistory(admin.value->userId).value->first().viewCount, qint64(1));

        f.window.reset();
        const auto reader = accounts.login("reader", "secret");
        QVERIFY(reader.status.ok() && reader.value);
        f.window = std::make_unique<LibraryWindow>(f.library, &accounts, &recommendations);
        f.window->show();
        QVERIFY(button(f.window.get(), QStringLiteral("新增"))->isHidden());
        QVERIFY(button(f.window.get(), QStringLiteral("删除"))->isHidden());
        QVERIFY(accounts.loadMediaHistory(reader.value->userId).value->isEmpty());
        f.window.reset();
    }

    void assistantShortcutsAndMissingKey() {
        Fixture f(QString::fromUtf8(SAMPLE_CSV));
        AccountHistoryServiceImpl accounts(f.library);
        QVERIFY(accounts.initializeDatabase(f.directory.filePath("assistant-accounts.sqlite")).ok());
        QVERIFY(accounts.createInitialAdmin("admin", "secret").ok());
        QVERIFY(accounts.registerUser("reader", "secret").ok());
        QVERIFY(accounts.login("reader", "secret").status.ok());

        LibraryWindow window(f.library, &accounts);
        window.show();
        auto* opener = window.findChild<QPushButton*>(QStringLiteral("openAssistant"));
        QVERIFY(opener);
        opener->click();
        auto* dialog = window.findChild<QDialog*>();
        QVERIFY(dialog && dialog->isVisible());
        auto* key = dialog->findChild<QLineEdit*>(QStringLiteral("assistantApiKey"));
        auto* input = dialog->findChild<QLineEdit*>(QStringLiteral("assistantInput"));
        auto* transcript = dialog->findChild<QTextEdit*>(QStringLiteral("assistantTranscript"));
        QVERIFY(key && input && transcript);
        QCOMPARE(key->echoMode(), QLineEdit::Password);
        key->clear();
        const auto welcome = transcript->toPlainText();
        input->setText(QStringLiteral("怎么搜索？"));
        button(dialog, QStringLiteral("发送"))->click();
        QCOMPARE(input->text(), QStringLiteral("怎么搜索？"));
        QCOMPARE(transcript->toPlainText(), welcome);
        auto* status = dialog->findChild<QLabel*>(QStringLiteral("assistantStatus"));
        QVERIFY(status && status->isVisible());
        QCOMPARE(status->text(), QStringLiteral("填入 DeepSeek API 密钥后即可发送。"));

        auto* tabs = window.findChild<QTabBar*>();
        auto* table = window.findChild<QTableWidget*>();
        auto* keyword = dialog->findChild<QLineEdit*>(QStringLiteral("assistantMediaKeyword"));
        auto* findMedia = button(dialog, QStringLiteral("查本地媒体"));
        QVERIFY(keyword && table);
        QVERIFY(!findMedia->isEnabled());
        searchInput(&window, QStringLiteral("导演"))->setText(QStringLiteral("旧筛选条件"));
        keyword->setText(QStringLiteral("  晨光.mp3  "));
        QVERIFY(findMedia->isEnabled());
        findMedia->click();
        QCOMPARE(tabs->currentIndex(), 1);
        QVERIFY(!dialog->isVisible());
        QCOMPARE(table->rowCount(), 1);
        QCOMPARE(table->item(0, 0)->text(), QStringLiteral("晨光.mp3"));
        QVERIFY(searchInput(&window, QStringLiteral("导演"))->text().isEmpty());

        opener->click();
        keyword->setText(QStringLiteral("不存在的助手测试片名"));
        findMedia->click();
        QCOMPARE(table->rowCount(), 0);
        QVERIFY(accounts.loadMediaHistory(accounts.currentUser()->userId).value->isEmpty());

        opener->click();
        QCOMPARE(window.findChild<QDialog*>(), dialog);
        button(dialog, QStringLiteral("新对话"))->click();
        QVERIFY(input->text().isEmpty());
        QCOMPARE(transcript->toPlainText(), welcome);
        button(dialog, QStringLiteral("打开推荐"))->click();
        QCOMPARE(tabs->currentIndex(), 2);
        QVERIFY(!dialog->isVisible());
    }

    void accountFrontendStartsRealSession() {
        Fixture f(QString::fromUtf8(SAMPLE_CSV));
        const auto database = f.directory.filePath("startup.sqlite");
        AccountHistoryServiceImpl accounts(f.library);
        QVERIFY(accounts.initializeDatabase(database).ok());
        QVERIFY(accounts.createInitialAdmin("admin", "secret").ok());
        recommendation_contract::PersonalRecommendationServiceImpl recommendations(
            accounts, [&] { return f.library.records(); });
        bool loginDialogSeen = false;
        bool sessionSeen = false;
        QTimer::singleShot(0, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) return;
            const auto fields = dialog->findChildren<QLineEdit*>();
            if (fields.size() < 2) { dialog->reject(); return; }
            loginDialogSeen = true;
            fields.at(0)->setText("admin");
            fields.at(1)->setText("secret");
            QTimer::singleShot(0, [&] {
                auto* session = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                if (!session) return;
                sessionSeen = session->findChild<QTableWidget*>() && accounts.currentUser().has_value();
                session->reject();
            });
            button(dialog, QStringLiteral("登录"))->click();
            if (dialog->isVisible()) dialog->reject();
        });
        QCOMPARE(runAccountFrontend(*qApp, f.library, accounts, recommendations, database), 0);
        QVERIFY(loginDialogSeen);
        QVERIFY(sessionSeen);
        QVERIFY(!accounts.currentUser().has_value());
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
