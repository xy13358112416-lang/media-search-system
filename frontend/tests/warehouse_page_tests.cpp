#include "../warehouse_page.h"
#include "../admin_dialog.h"
#include <QTableWidget>
#include "../participation_settings_dialog.h"
#include <QCheckBox>
#include <QDialog>
#include "online_care_client.h"
#include "../footprint_page.h"
#include <memory>
#include "warehouse_server.h"

#include <QCheckBox>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

using namespace warehouse_contract;

namespace {

template<typename Widget>
Widget* named(WarehousePage& page, const char* name)
{
    return page.findChild<Widget*>(QString::fromLatin1(name));
}

template<typename Widget>
Widget* matching(QWidget& page, const QString& text)
{
    for (auto* widget : page.findChildren<Widget*>())
        if (widget->text() == text)
            return widget;
    return nullptr;
}

// The integration test uses a TCP server for warehouse operations; recommendation
// responses remain controlled to exercise cancellation and explicit confirmation.
class TestClient : public WarehouseClient {
public:
    RecommendationHandle recommendResources(const WarehouseRecommendationRequest& request,
        Callback<WarehouseRecommendationResult> done) override
    {
        lastRequest = request;
        callback = std::move(done);
        return QStringLiteral("test-recommendation");
    }

    bool cancelRecommendation(const RecommendationHandle& handle) override
    {
        cancelled = handle;
        return !handle.isEmpty();
    }

    RecommendationHandle cancelled;
    WarehouseRecommendationRequest lastRequest;
    Callback<WarehouseRecommendationResult> callback;
};

// Deterministic page tests can finish a list request while a confirmation is open.
class DeferredPageClient final : public TestClient {
public:
    void connectToServer(const QString&, quint16, StatusCallback done) override { done({}); }
    void login(const QString&, const QString&, Callback<UserInfo> done) override
    { done({{}, UserInfo{7, QStringLiteral("reader")}}); }
    void listMyWarehouses(Callback<QList<WarehouseSummary>> done) override
    {
        if (deferLists)
            listDone = std::move(done);
        else
            done({{}, warehouses});
    }
    void getWarehouse(qint64 id, Callback<WarehouseDetail> done) override
    {
        for (const auto& warehouse : warehouses) {
            if (warehouse.warehouseId == id) {
                done({{}, WarehouseDetail{warehouse, {}}});
                return;
            }
        }
        done({{ErrorCode::NotFound, QStringLiteral("仓库不存在")}, {}});
    }
    void addItem(qint64 id, const MediaRecord& media, Callback<WarehouseItem> done) override
    {
        addedTo.append(id);
        done({{}, WarehouseItem{1, media}});
    }
    void completeList(QList<WarehouseSummary> result)
    {
        warehouses = std::move(result);
        auto done = std::move(listDone);
        listDone = {};
        done({{}, warehouses});
    }

    bool deferLists = false;
    QList<WarehouseSummary> warehouses{
        {1, QStringLiteral("A"), 7, Visibility::Private},
        {2, QStringLiteral("B"), 7, Visibility::Private}};
    Callback<QList<WarehouseSummary>> listDone;
    QList<qint64> addedTo;
};

void signIn(WarehousePage& page, quint16 port, const QString& user, const QString& password)
{
    named<QSpinBox>(page, "warehousePort")->setValue(port);
    named<QLineEdit>(page, "warehouseUsername")->setText(user);
    named<QLineEdit>(page, "warehousePassword")->setText(password);
    matching<QPushButton>(page, QStringLiteral("连接并登录"))->click();
}

} // namespace

class OnlineWarehouseTests : public QObject {
    Q_OBJECT
private slots:
    void administratorIdentityAndUserDialog()
    {
        QTemporaryDir temporary;
        WarehouseServer server(llm_recommendation::Config{});
        QString error;
        QVERIFY(server.openDatabase(temporary.filePath("admin.sqlite"), error));
        QVERIFY(server.addUser("operator", "admin-pass", error, true));
        QVERIFY(server.addUser("reader", "reader-pass", error));
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        OnlineCareClient client;
        std::optional<Status> connected;
        client.connectToServer("127.0.0.1", server.serverPort(), [&](auto result) { connected = result; });
        QTRY_VERIFY(connected && connected->ok());
        std::optional<Result<UserInfo>> loggedIn;
        client.loginAs("operator", "admin-pass", LoginRole::Administrator, [&](auto result) { loggedIn = result; });
        QTRY_VERIFY(loggedIn && loggedIn->status.ok() && loggedIn->value);
        QVERIFY(loggedIn->value->isAdmin);
        QDialog parent;
        bool foundUsers = false;
        QTimer poll;
        connect(&poll, &QTimer::timeout, &parent, [&] {
            for (auto* window : QApplication::topLevelWidgets()) {
                auto* dialog = qobject_cast<QDialog*>(window);
                if (!dialog || dialog->windowTitle() != QStringLiteral("用户管理")) continue;
                auto* table = dialog->findChild<QTableWidget*>();
                if (table && table->rowCount() == 2) {
                    foundUsers = table->item(0, 2)->text() == QStringLiteral("管理员");
                    dialog->reject();
                }
            }
        });
        QTimer::singleShot(5000, &parent, [&] {
            for (auto* window : QApplication::topLevelWidgets())
                if (window->windowTitle() == QStringLiteral("用户管理")) window->close();
        });
        poll.start(10);
        showAdminDialog(client, &parent);
        QVERIFY(foundUsers);
    }
    void onlineViewsComeOnlyFromClicksAndFollowTheServerAccount()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        WarehouseServer server(llm_recommendation::Config{});
        QString error;
        QVERIFY2(server.openDatabase(temporary.filePath("clicks.sqlite"), error), qPrintable(error));
        QVERIFY(server.addUser("alice", "alice-demo", error));
        QVERIFY(server.addUser("bob", "bob-demo", error));
        QVERIFY(server.listen(QHostAddress::LocalHost));

        OnlineCareClient client;
        WarehousePage page(client, [] { return QList<MediaRecord>{}; });
        client.onDisconnected(&page, [&] { page.connectionLost(); });
        page.show();
        std::unique_ptr<CarePage> footprint(createFootprintPage(client, [&] { return page.isSignedIn(); }));
        auto* footprintStatus = footprint->findChild<QLabel*>("careStatus");
        QVERIFY(footprintStatus);
        page.setSessionChangedCallback([&] {
            footprint->deactivate();
            footprint->activate();
        });
        signIn(page, server.serverPort(), "alice", "alice-demo");
        QTRY_VERIFY(page.isSignedIn());

        std::optional<Result<WarehouseSummary>> warehouse;
        client.createWarehouse("Alice", Visibility::Private, [&](auto result) { warehouse = result; });
        QTRY_VERIFY(warehouse && warehouse->status.ok() && warehouse->value);
        MediaRecord media;
        media.id = "demo:media";
        media.fileName = "Quiet Film";
        media.category = QStringLiteral("电影");
        std::optional<Result<WarehouseItem>> bought;
        client.addItem(warehouse->value->warehouseId, media, [&](auto result) { bought = result; });
        QTRY_VERIFY(bought && bought->status.ok() && bought->value);

        auto* refresh = matching<QPushButton>(page, QStringLiteral("刷新列表"));
        auto* items = named<QListWidget>(page, "onlineWarehouseItems");
        QVERIFY(refresh && items);
        refresh->click();
        QTRY_COMPARE(items->count(), 1);
        items->setCurrentRow(0);
        refresh->click();
        QTRY_COMPARE(items->count(), 1);
        items->setCurrentRow(0);

        using namespace care_contract;
        const auto date = care_ui::localTime(QDateTime::currentSecsSinceEpoch()).date();
        const FootprintRequest request{{care_ui::midnight(date), care_ui::midnight(date.addDays(1))}, std::nullopt};
        std::optional<warehouse_contract::Result<FootprintReport>> report;
        const auto requestReport = [&] {
            report.reset();
            client.getFootprint(request, [&](auto result) { report = std::move(result); });
        };
        requestReport();
        QTRY_VERIFY(report && report->status.ok() && report->value);
        QCOMPARE(report->value->current.views, qint64(0));

        QTest::mouseClick(items->viewport(), Qt::LeftButton, Qt::NoModifier,
                          items->visualItemRect(items->item(0)).center());
        // The first read may arrive before the resource resolution has queued the write.
        requestReport();
        QTRY_VERIFY(report && report->status.ok() && report->value);
        requestReport();
        QTRY_VERIFY(report && report->status.ok() && report->value);
        QCOMPARE(report->value->current.views, qint64(1));
        footprint->activate();
        QTRY_VERIFY(footprintStatus->text().contains(QStringLiteral("北京时间")));
        const auto* counts = footprint->findChild<QLabel*>("footprintCounts");
        QVERIFY(counts && counts->text().contains(QStringLiteral("浏览 1 次")));

        auto* query = named<QLineEdit>(page, "warehouseRecommendationQuery");
        auto* recommendation = named<QListWidget>(page, "onlineWarehouseRecommendations");
        query->setText("Quiet Film");
        matching<QPushButton>(page, QStringLiteral("获取推荐"))->click();
        QTRY_COMPARE(recommendation->count(), 1);
        auto* note = named<QLabel>(page, "warehouseRecommendationStatus");
        QVERIFY(note->text().contains(QStringLiteral("备用推荐")));

        auto* logout = matching<QPushButton>(page, QStringLiteral("退出仓库"));
        auto* login = matching<QPushButton>(page, QStringLiteral("连接并登录"));
        logout->click();
        QTRY_VERIFY(login->isEnabled());
        QCOMPARE(recommendation->count(), 0);
        QVERIFY(note->text().isEmpty());
        QVERIFY(counts->text().isEmpty());
        signIn(page, server.serverPort(), "bob", "bob-demo");
        QTRY_VERIFY(page.isSignedIn());
        requestReport();
        QTRY_VERIFY(report && report->status.ok() && report->value);
        QCOMPARE(report->value->current.views, qint64(0));
        QCOMPARE(items->count(), 0);
        QTRY_VERIFY(counts->text().contains(QStringLiteral("浏览 0 次")));

        const auto sockets = server.findChildren<QTcpSocket*>();
        QVERIFY(!sockets.isEmpty());
        for (auto* socket : sockets)
            socket->disconnectFromHost();
        QTRY_VERIFY(!page.isSignedIn() && login->isEnabled());
        QVERIFY(counts->text().isEmpty());
        QVERIFY(footprintStatus->text().contains(QStringLiteral("登录服务器账号")));
    }

    void publicSearchAndSettingsUseCurrentServerAccount()
    {
        QTemporaryDir temporary;
        WarehouseServer server(llm_recommendation::Config{});
        QString error;
        QVERIFY2(server.openDatabase(temporary.filePath("public-search.sqlite"), error), qPrintable(error));
        QVERIFY(server.addUser("alice", "alice-demo", error));
        QVERIFY(server.addUser("bob", "bob-demo", error));
        QVERIFY(server.addUser("carol", "carol-demo", error));
        QVERIFY(server.listen(QHostAddress::LocalHost));

        OnlineCareClient client;
        WarehousePage page(client, [] { return QList<MediaRecord>{}; });
        page.show();
        signIn(page, server.serverPort(), "alice", "alice-demo");
        QTRY_VERIFY(page.isSignedIn());

        auto* dialog = createParticipationSettingsDialog(client, &page);
        dialog->open();
        auto* history = matching<QCheckBox>(*dialog, QStringLiteral("保存浏览和搜索历史"));
        auto* personalization = matching<QCheckBox>(*dialog, QStringLiteral("根据历史生成个性化推荐"));
        auto* contribution = matching<QCheckBox>(*dialog, QStringLiteral("允许行为计入公开热榜"));
        auto* save = matching<QPushButton>(*dialog, QStringLiteral("保存设置"));
        QVERIFY(history && personalization && contribution && save);
        QTRY_VERIFY(save->isEnabled());
        QVERIFY(history->isChecked());
        QVERIFY(personalization->isChecked());
        QVERIFY(!contribution->isChecked());
        personalization->setChecked(false);
        contribution->setChecked(true);
        QSignalSpy settingsSaved(dialog, &QDialog::accepted);
        save->click();
        QTRY_COMPARE(settingsSaved.count(), 1);

        int created = 0;
        std::optional<Result<WarehouseSummary>> publicWarehouse;
        client.createWarehouse("Room", Visibility::Public, [&](auto result) {
            publicWarehouse = std::move(result);
        });
        client.createWarehouse("Private room", Visibility::Private, [&](auto result) {
            if (result.status.ok()) ++created;
        });
        QTRY_VERIFY(publicWarehouse && publicWarehouse->status.ok() && publicWarehouse->value);
        QTRY_COMPARE(created, 1);
        std::optional<Result<WarehouseItem>> item;
        MediaRecord media;
        media.id = "demo:search";
        media.fileName = "Quiet Film";
        client.addItem(publicWarehouse->value->warehouseId, media, [&](auto result) { item = std::move(result); });
        QTRY_VERIFY(item && item->status.ok() && item->value);
        auto* publicOnly = matching<QCheckBox>(page, QStringLiteral("浏览所有公开仓库"));
        auto* search = named<QLineEdit>(page, "warehousePublicSearch");
        auto* submit = matching<QPushButton>(page, QStringLiteral("搜索当前公开仓库"));
        auto* clear = matching<QPushButton>(page, QStringLiteral("清除搜索"));
        auto* items = named<QListWidget>(page, "onlineWarehouseItems");
        auto* warehouses = named<QListWidget>(page, "onlineWarehouseList");
        QVERIFY(publicOnly && search && submit && clear && items && warehouses);
        publicOnly->setChecked(true);
        QTRY_COMPARE(items->count(), 1);
        QTRY_VERIFY(search->isVisible());
        search->setText("quiet");

        using namespace care_contract;
        const auto date = care_ui::localTime(QDateTime::currentSecsSinceEpoch()).date();
        const TimeRange range{care_ui::midnight(date), care_ui::midnight(date.addDays(1))};
        auto verifySearches = [&](qint64 expected) {
            std::optional<warehouse_contract::Result<FootprintReport>> report;
            client.getFootprint({range, {}}, [&](auto result) { report = std::move(result); });
            QTRY_VERIFY(report && report->status.ok() && report->value);
            QCOMPARE(report->value->current.views, qint64(0));
            QCOMPARE(report->value->current.searches, expected);
        };
        verifySearches(0); // Typing did not send a search.
        submit->click();
        QVERIFY(!items->item(0)->isHidden());
        verifySearches(1);

        std::optional<warehouse_contract::Result<TrendingResult>> trend;
        client.getTrending({TrendingKind::SearchTerms, range, 20}, [&](auto result) { trend = std::move(result); });
        QTRY_VERIFY(trend && trend->status.ok() && trend->value);
        QVERIFY(trend->value->items.isEmpty()); // A single member cannot publish a term.

        for (const QString& name : {QStringLiteral("bob"), QStringLiteral("carol")}) {
            std::optional<Status> status;
            std::optional<warehouse_contract::Result<UserInfo>> login;
            OnlineCareClient contributor;
            contributor.connectToServer("127.0.0.1", server.serverPort(), [&](auto result) { status = result; });
            QTRY_VERIFY(status && status->ok());
            contributor.login(name, name + "-demo", [&](auto result) { login = std::move(result); });
            QTRY_VERIFY(login && login->status.ok());
            status.reset();
            contributor.setParticipationSettings({true, true, true}, [&](auto result) { status = result; });
            QTRY_VERIFY(status && status->ok());
            status.reset();
            contributor.recordBehavior({name + "-search", BehaviorAction::Search, std::nullopt, "Quiet"},
                                       [&](auto result) { status = result; });
            QTRY_VERIFY(status && status->ok());
        }
        trend.reset();
        client.getTrending({TrendingKind::SearchTerms, range, 20}, [&](auto result) { trend = std::move(result); });
        QTRY_VERIFY(trend && trend->status.ok() && trend->value);
        QCOMPARE(trend->value->items.size(), 1);
        QCOMPARE(trend->value->items.first().term, QStringLiteral("quiet"));
        QCOMPARE(trend->value->items.first().distinctUsers, qint64(3));

        search->setText("missing");
        QTest::keyClick(search, Qt::Key_Return);
        QVERIFY(items->item(0)->isHidden());
        clear->click();
        QVERIFY(!items->item(0)->isHidden());
        verifySearches(2);
        publicOnly->setChecked(false);
        QTRY_COMPARE(warehouses->count(), 2);
        for (int row = 0; row < warehouses->count(); ++row)
            if (warehouses->item(row)->text().startsWith("Private room"))
                warehouses->setCurrentRow(row);
        QTRY_VERIFY(!search->isVisible());
        verifySearches(2);

        matching<QPushButton>(page, QStringLiteral("退出仓库"))->click();
        QTRY_VERIFY(matching<QPushButton>(page, QStringLiteral("连接并登录"))->isEnabled());
        signIn(page, server.serverPort(), "alice", "alice-demo");
        QTRY_VERIFY(page.isSignedIn());
        std::optional<warehouse_contract::Result<ParticipationSettings>> settings;
        client.getParticipationSettings([&](auto result) { settings = std::move(result); });
        QTRY_VERIFY(settings && settings->status.ok() && settings->value);
        QVERIFY(!settings->value->personalizationEnabled);
        QVERIFY(settings->value->contributePublicTrends);

        std::optional<Status> paused;
        client.setParticipationSettings({false, false, false}, [&](auto result) { paused = result; });
        QTRY_VERIFY(paused && paused->ok());
        publicOnly->setChecked(true);
        QTRY_VERIFY(search->isVisible());
        search->setText("quiet");
        submit->click();
        verifySearches(2);
        QTest::mouseClick(items->viewport(), Qt::LeftButton, Qt::NoModifier,
                          items->visualItemRect(items->item(0)).center());
        verifySearches(2); // First read can precede resource resolution.
        verifySearches(2);
        trend.reset();
        client.getTrending({TrendingKind::SearchTerms, range, 20}, [&](auto result) { trend = std::move(result); });
        QTRY_VERIFY(trend && trend->status.ok() && trend->value);
        QVERIFY(trend->value->items.isEmpty()); // Alice withdrew her previous public contributions.
    }

    void refreshKeepsSelectionChangedWhileLoading()
    {
        DeferredPageClient client;
        WarehousePage page(client, [] { return QList<MediaRecord>{}; });
        signIn(page, 8765, QStringLiteral("reader"), QStringLiteral("secret"));
        auto* warehouses = named<QListWidget>(page, "onlineWarehouseList");
        QCOMPARE(warehouses->currentRow(), 0);
        client.deferLists = true;
        matching<QPushButton>(page, QStringLiteral("刷新列表"))->click();
        QVERIFY(client.listDone);
        warehouses->setCurrentRow(1);
        named<QLineEdit>(page, "warehouseRecommendationQuery")->setText(QStringLiteral("轻松电影"));
        matching<QPushButton>(page, QStringLiteral("获取推荐"))->click();
        QVERIFY(client.lastRequest.targetWarehouseId);
        QCOMPARE(*client.lastRequest.targetWarehouseId, qint64(2));

        client.completeList(client.warehouses);
        QCOMPARE(warehouses->currentRow(), 1);
        auto done = std::move(client.callback);
        QVERIFY(done);
        const WarehouseRecommendationItem suggestion{2, 1, {}, QStringLiteral("适合放松")};
        done({{}, WarehouseRecommendationResult{{suggestion}, false, {}}});
        QCOMPARE(named<QListWidget>(page, "onlineWarehouseRecommendations")->count(), 1);
        QVERIFY(client.cancelled.isEmpty());
        QVERIFY(client.addedTo.isEmpty());
    }

    void confirmationRejectsWarehouseChangedByListResponse()
    {
        DeferredPageClient client;
        WarehousePage page(client, [] { return QList<MediaRecord>{}; });
        signIn(page, 8765, QStringLiteral("reader"), QStringLiteral("secret"));
        auto* warehouses = named<QListWidget>(page, "onlineWarehouseList");
        warehouses->setCurrentRow(1);
        matching<QCheckBox>(page, QStringLiteral("使用所选仓库"))->setChecked(false);
        named<QLineEdit>(page, "warehouseRecommendationQuery")->setText(QStringLiteral("轻松电影"));
        matching<QPushButton>(page, QStringLiteral("获取推荐"))->click();
        auto done = std::move(client.callback);
        QVERIFY(done);
        const WarehouseRecommendationItem suggestion{2, 1, {}, QStringLiteral("适合放松")};
        done({{}, WarehouseRecommendationResult{{suggestion}, false, {}}});
        named<QListWidget>(page, "onlineWarehouseRecommendations")->setCurrentRow(0);

        client.deferLists = true;
        matching<QPushButton>(page, QStringLiteral("刷新列表"))->click();
        QVERIFY(client.listDone);
        QTimer::singleShot(0, &page, [&] {
            client.completeList({client.warehouses.first()});
            auto* dialog = page.findChild<QMessageBox*>();
            QVERIFY(dialog);
            dialog->button(QMessageBox::Yes)->click();
        });
        matching<QPushButton>(page, QStringLiteral("确认加入所选仓库"))->click();
        QCOMPARE(warehouses->currentRow(), 0);
        QVERIFY(client.addedTo.isEmpty());
    }

    void twoAccountsAndRecommendationConfirm()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QString error;
        WarehouseServer server;
        QVERIFY2(server.openDatabase(temporary.filePath("warehouse.sqlite"), error), qPrintable(error));
        QVERIFY(server.addUser("alice", "alice-demo", error));
        QVERIFY(server.addUser("bob", "bob-demo", error));
        QVERIFY(server.listen(QHostAddress::LocalHost));

        const MediaRecord item{"m1", "Example.mp4", "MP4", 100, "Director", {"Actor"},
                               2, QDate(2025, 1, 1), {"Award"}, QStringLiteral("电影")};
        TestClient aliceClient;
        WarehouseClient bobClient;
        WarehousePage alice(aliceClient, [&] { return QList<MediaRecord>{item}; });
        WarehousePage bob(bobClient, [] { return QList<MediaRecord>{}; });
        alice.show();
        bob.show();

        signIn(alice, server.serverPort(), "alice", "alice-demo");
        signIn(bob, server.serverPort(), "bob", "bob-demo");
        auto* create = matching<QPushButton>(alice, QStringLiteral("新建仓库"));
        QVERIFY(create);
        QTRY_VERIFY(create->isEnabled());
        auto* publicOnly = matching<QCheckBox>(bob, QStringLiteral("浏览所有公开仓库"));
        QVERIFY(publicOnly);
        QTRY_VERIFY(publicOnly->isEnabled());
        QCOMPARE(named<QLineEdit>(alice, "warehousePassword")->text(), QString());

        QTimer::singleShot(0, &alice, [&alice] {
            auto* dialog = alice.findChild<QDialog*>();
            dialog->findChild<QLineEdit*>("newWarehouseName")->setText(QStringLiteral("公开馆"));
            matching<QCheckBox>(*dialog, QStringLiteral("允许其他账号查看"))->setChecked(true);
            matching<QPushButton>(*dialog, QStringLiteral("创建"))->click();
        });
        create->click();
        auto* aliceWarehouses = named<QListWidget>(alice, "onlineWarehouseList");
        auto* aliceItems = named<QListWidget>(alice, "onlineWarehouseItems");
        auto* bobWarehouses = named<QListWidget>(bob, "onlineWarehouseList");
        auto* bobItems = named<QListWidget>(bob, "onlineWarehouseItems");
        QTRY_COMPARE(aliceWarehouses->count(), 1);
        QCOMPARE(bobWarehouses->count(), 0);

        QTimer::singleShot(0, &alice, [&alice] {
            alice.findChild<QInputDialog*>()->accept();
        });
        matching<QPushButton>(alice, QStringLiteral("添加本地作品"))->click();
        QTRY_COMPARE(aliceItems->count(), 1);

        publicOnly->setChecked(true);
        QTRY_COMPARE(bobWarehouses->count(), 1);
        QTRY_COMPARE(bobItems->count(), 1);
        bobItems->setCurrentRow(0);
        QVERIFY(!matching<QPushButton>(bob, QStringLiteral("添加本地作品"))->isEnabled());
        QVERIFY(!matching<QPushButton>(bob, QStringLiteral("删除条目"))->isEnabled());
        publicOnly->setChecked(false);
        QCOMPARE(bobWarehouses->accessibleName(), QStringLiteral("我的仓库"));
        QCOMPARE(bobWarehouses->count(), 0);
        QCOMPARE(bobItems->count(), 0);
        QTRY_COMPARE(named<QLabel>(bob, "warehouseStatus")->text(), QStringLiteral("你还没有仓库。"));
        QCOMPARE(aliceWarehouses->count(), 1);

        auto* query = named<QLineEdit>(alice, "warehouseRecommendationQuery");
        auto* recommend = matching<QPushButton>(alice, QStringLiteral("获取推荐"));
        auto* suggestions = named<QListWidget>(alice, "onlineWarehouseRecommendations");
        query->setText(QStringLiteral("轻松电影"));
        recommend->click();
        QVERIFY(aliceClient.lastRequest.targetWarehouseId);
        QCOMPARE(aliceClient.lastRequest.query, query->text());
        QVERIFY(aliceClient.callback);
        alice.suspend();
        QCOMPARE(aliceClient.cancelled, QStringLiteral("test-recommendation"));
        auto late = std::move(aliceClient.callback);
        const WarehouseRecommendationItem suggestion{1, 1, item, QStringLiteral("适合轻松浏览")};
        late({{}, WarehouseRecommendationResult{{suggestion}, false, {}}});
        QCOMPARE(suggestions->count(), 0);

        matching<QCheckBox>(alice, QStringLiteral("使用所选仓库"))->setChecked(false);
        recommend->click();
        QVERIFY(!aliceClient.lastRequest.targetWarehouseId);
        auto fresh = std::move(aliceClient.callback);
        fresh({{}, WarehouseRecommendationResult{{}, false, QStringLiteral("没有匹配")}});
        QCOMPARE(suggestions->count(), 0);
        QCOMPARE(named<QLabel>(alice, "warehouseRecommendationStatus")->text(), QStringLiteral("没有匹配"));
        recommend->click();
        fresh = std::move(aliceClient.callback);
        fresh({{}, WarehouseRecommendationResult{{suggestion}, true, QStringLiteral("模型暂不可用")}});
        QCOMPARE(suggestions->count(), 1);
        QVERIFY(named<QLabel>(alice, "warehouseRecommendationStatus")->text().contains(QStringLiteral("备用推荐")));
        QCOMPARE(aliceItems->count(), 1);
        suggestions->setCurrentRow(0);

        const auto confirmYes = [&alice] {
            auto* dialog = alice.findChild<QMessageBox*>();
            dialog->button(QMessageBox::Yes)->click();
        };
        QTimer::singleShot(0, &alice, confirmYes);
        matching<QPushButton>(alice, QStringLiteral("确认加入所选仓库"))->click();
        QTRY_COMPARE(aliceItems->count(), 2);

        aliceItems->setCurrentRow(0);
        QTimer::singleShot(0, &alice, confirmYes);
        matching<QPushButton>(alice, QStringLiteral("删除条目"))->click();
        QTRY_COMPARE(aliceItems->count(), 1);

        recommend->click();
        matching<QPushButton>(alice, QStringLiteral("退出仓库"))->click();
        auto logoutResult = std::move(aliceClient.callback);
        logoutResult({{}, WarehouseRecommendationResult{{suggestion}, false, {}}});
        QCOMPARE(suggestions->count(), 0);
        QCOMPARE(aliceWarehouses->count(), 0);
        QVERIFY(create && !create->isEnabled());
        QVERIFY(publicOnly->isEnabled());
    }
};

QTEST_MAIN(OnlineWarehouseTests)
#include "warehouse_page_tests.moc"
