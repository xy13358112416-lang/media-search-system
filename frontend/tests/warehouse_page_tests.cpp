#include "../warehouse_page.h"
#include "warehouse_client.h"
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

// The warehouse operations use an actual TCP server. Only the unfinished R03
// recommendation endpoint is supplied by this test double.
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
        auto* publicOnly = matching<QCheckBox>(bob, QStringLiteral("查看公开仓库"));
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
        QCOMPARE(named<QLabel>(alice, "warehouseStatus")->text(), QStringLiteral("没有匹配"));
        recommend->click();
        fresh = std::move(aliceClient.callback);
        fresh({{}, WarehouseRecommendationResult{{suggestion}, true, QStringLiteral("模型暂不可用")}});
        QCOMPARE(suggestions->count(), 1);
        QVERIFY(named<QLabel>(alice, "warehouseStatus")->text().contains(QStringLiteral("备用推荐")));
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
