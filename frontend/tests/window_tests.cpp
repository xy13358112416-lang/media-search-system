#include "account_history_service.h"
#include "personalrecommendation_service.h"
#include "window_test_support.h"

#include <QCheckBox>
#include <QPropertyAnimation>
#include <QSettings>
#include <QtTest>

using namespace UiTest;

class WindowTests : public QObject {
    Q_OBJECT
    QTemporaryDir settingsDir_;
private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
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

    void warmScenesAndMotionPreference() {
        using warm_mode::Scene;
        using media_mood::Mood;
        const auto noRecommendations = [] {};
        QList<Scene> scenes;
        Mood mood = Mood::Any;
        const auto search = [&](const QString&, Mood value, const QString&, bool) { mood = value; };
        const auto sceneChanged = [&](Scene scene) { scenes.append(scene); };
        {
            AssistantDialog dialog(nullptr, search, sceneChanged, noRecommendations, QStringLiteral("推荐"));
            dialog.show();
            auto* motion = dialog.findChild<QCheckBox*>(QStringLiteral("warmAnimationEnabled"));
            auto* animation = dialog.findChild<QPropertyAnimation*>(QStringLiteral("warmSceneAnimation"));
            auto* panel = dialog.findChild<QFrame*>(QStringLiteral("warmSceneCard"));
            QVERIFY(motion && animation && panel);
            QCOMPARE(scenes.last(), Scene::Neutral);
            QVERIFY(motion->isChecked());
            const auto neutral = panel->styleSheet();
            button(&dialog, QStringLiteral("放松"))->click();
            QCOMPARE(scenes.last(), Scene::Relax);
            QVERIFY(panel->styleSheet() != neutral);
            QCOMPARE(animation->state(), QAbstractAnimation::Running);
            motion->setChecked(false);
            QCOMPARE(animation->state(), QAbstractAnimation::Stopped);
            button(&dialog, QStringLiteral("查本地媒体"))->click();
            QCOMPARE(mood, Mood::Relaxing);
            dialog.show();
            for (const auto& label : {QStringLiteral("专注"), QStringLiteral("探索"), QStringLiteral("默认")}) {
                button(&dialog, label)->click();
                QCOMPARE(animation->state(), QAbstractAnimation::Stopped);
            }
            QCOMPARE(scenes.mid(2), (QList<Scene>{Scene::Focus, Scene::Explore, Scene::Neutral}));
            QCOMPARE(panel->styleSheet(), neutral);
            button(&dialog, QStringLiteral("查本地媒体"))->click();
            QCOMPARE(mood, Mood::Any);
        }
        AssistantDialog dialog(nullptr, search, sceneChanged, noRecommendations, QStringLiteral("推荐"));
        auto* motion = dialog.findChild<QCheckBox*>(QStringLiteral("warmAnimationEnabled"));
        QVERIFY(motion && !motion->isChecked());
        QCOMPARE(scenes.last(), Scene::Neutral);
    }

    void assistantSearchesLocalMediaByMood() {
        Fixture f(QString::fromUtf8(REAL_CSV));
        AccountHistoryServiceImpl accounts(f.library);
        QVERIFY(accounts.initializeDatabase(f.directory.filePath("assistant-accounts.sqlite")).ok());
        QVERIFY(accounts.createInitialAdmin("admin", "secret").ok());
        QVERIFY(accounts.registerUser("reader", "secret").ok());
        QVERIFY(accounts.login("reader", "secret").status.ok());

        LibraryWindow window(f.library, &accounts);
        window.show();
        auto* opener = window.findChild<QPushButton*>(QStringLiteral("openAssistant"));
        QVERIFY(opener);
        QVERIFY(!window.findChild<QDialog*>());
        opener->click();
        auto* dialog = window.findChild<QDialog*>();
        QVERIFY(dialog && dialog->isVisible());
        QVERIFY(!dialog->findChild<QLineEdit*>(QStringLiteral("assistantApiKey")));
        auto* tabs = window.findChild<QTabBar*>();
        auto* table = window.findChild<QTableWidget*>();
        auto* mood = dialog->findChild<QComboBox*>(QStringLiteral("assistantMood"));
        auto* category = dialog->findChild<QComboBox*>(QStringLiteral("assistantCategory"));
        auto* keyword = dialog->findChild<QLineEdit*>(QStringLiteral("assistantMediaKeyword"));
        QVERIFY(tabs && table && mood && category && keyword);
        const auto choose = [](QComboBox* box, const QVariant& selection) {
            box->setCurrentIndex(box->findData(selection));
        };
        choose(mood, static_cast<int>(media_mood::Mood::Energizing));
        choose(category, QStringLiteral("音乐"));
        keyword->setText(QStringLiteral("  Champions  "));
        button(dialog, QStringLiteral("查本地媒体"))->click();
        QCOMPARE(tabs->currentIndex(), 1);
        QVERIFY(!dialog->isVisible());
        QCOMPARE(table->rowCount(), 1);
        QCOMPARE(table->item(0, 0)->text(), QStringLiteral("We Are the Champions"));

        opener->click();
        choose(category, QStringLiteral("电影"));
        button(dialog, QStringLiteral("查本地媒体"))->click();
        QCOMPARE(table->rowCount(), 0);
        QVERIFY(accounts.loadMediaHistory(accounts.currentUser()->userId).value->isEmpty());

        button(&window, QStringLiteral("清空条件"))->click();
        QCOMPARE(table->rowCount(), f.library.records().size());
        opener->click();
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

    void integrationKeepsTodayAssistantAndOnlineWarehouse() {
        Fixture f(QString::fromUtf8(SAMPLE_CSV));
        AccountHistoryServiceImpl accounts(f.library);
        QVERIFY(accounts.initializeDatabase(f.directory.filePath("integrated-account.sqlite")).ok());
        QVERIFY(accounts.createInitialAdmin("admin", "secret").ok());
        QVERIFY(accounts.login("admin", "secret").status.ok());
        recommendation_contract::PersonalRecommendationServiceImpl recommendations(
            accounts, [&] { return f.library.records(); });
        recommendation_contract::TodayRecommendationServiceImpl today(
            recommendations, [&] { return f.library.records(); });
        LibraryWindow window(f.library, &accounts, &recommendations, &today);
        window.show();
        auto* tabs = window.findChild<QTabBar*>();
        QVERIFY(tabs);
        QCOMPARE(tabs->count(), 8);
        QCOMPARE(tabs->tabText(2), QStringLiteral("今日推荐"));
        QCOMPARE(tabs->tabText(3), QStringLiteral("资源仓库"));
        QCOMPARE(tabs->tabText(4), QStringLiteral("个人足迹"));
        auto* host = window.findChild<QLineEdit*>(QStringLiteral("warehouseHost"));
        QVERIFY(host); // Real WarehousePage, rather than the old placeholder.
        tabs->setCurrentIndex(2);
        QVERIFY(!window.findChildren<QFrame*>(QStringLiteral("recommendCard")).isEmpty());
        QVERIFY(!host->isVisible());
        tabs->setCurrentIndex(3);
        QVERIFY(host->isVisible());
        const auto* signedOut = window.findChild<QLabel*>(QStringLiteral("onlineCareSignedOut"));
        const auto* settings = window.findChild<QPushButton*>(QStringLiteral("onlineParticipationSettings"));
        QVERIFY(signedOut && signedOut->text().contains(QStringLiteral("登录服务器账号")));
        QVERIFY(settings);
        QCOMPARE(tabs->tabText(5), QStringLiteral("全站热榜"));
        QCOMPARE(tabs->tabText(6), QStringLiteral("动态推荐"));
        QCOMPARE(tabs->tabText(7), QStringLiteral("关注中心"));
        for (int index : {4, 5, 6, 7}) {
            tabs->setCurrentIndex(index);
            QVERIFY(signedOut->isVisible());
            QVERIFY(!settings->isVisible());
            QVERIFY(!host->isVisible());
        }
        tabs->setCurrentIndex(3);
        QVERIFY(!signedOut->isVisible());
        QVERIFY(host->isVisible());
        for (auto* card : window.findChildren<QFrame*>(QStringLiteral("recommendCard")))
            QVERIFY(!card->isVisible());
        auto* assistant = window.findChild<QPushButton*>(QStringLiteral("openAssistant"));
        QVERIFY(assistant);
        assistant->click();
        auto* dialog = window.findChild<QDialog*>();
        QVERIFY(dialog);
        button(dialog, QStringLiteral("打开今日推荐"))->click();
        QCOMPARE(tabs->currentIndex(), 2);
        QVERIFY(!host->isVisible());
        tabs->setCurrentIndex(0);
        auto* table = window.findChild<QTableWidget*>();
        QVERIFY(table && table->isVisible());
        QCOMPARE(warm_mode::browseScene(warm_mode::Scene::Neutral), care_contract::BrowseScene::Neutral);
        QCOMPARE(warm_mode::browseScene(warm_mode::Scene::Relax), care_contract::BrowseScene::Relax);
        QCOMPARE(warm_mode::browseScene(warm_mode::Scene::Focus), care_contract::BrowseScene::Focus);
        QCOMPARE(warm_mode::browseScene(warm_mode::Scene::Explore), care_contract::BrowseScene::Explore);
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
