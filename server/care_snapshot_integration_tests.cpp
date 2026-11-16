// Exercise the existing TCP warehouse boundary and the future care snapshot
// boundary together. Behavioral persistence/RPC (R05) is deliberately not faked.
#include "warehouse_server.h"
#include "warehouse_client.h"
#include "care_footprint.h"
#include "care_person.h"
#include "care_temporal.h"
#include "care_trending.h"
#include <QEventLoop>
#include <QMap>
#include <QPointer>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

namespace wc = warehouse_contract;
namespace cc = care_contract;
namespace ca = care_algorithms;
namespace {
template<class T, class Start> wc::Result<T> awaitResult(Start start) {
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    auto result = std::make_shared<wc::Result<T>>(wc::Result<T>{{wc::ErrorCode::Timeout, "test timeout"}, {}});
    auto completed = std::make_shared<bool>(false);
    start([result, completed, guard = QPointer<QEventLoop>(&loop)](wc::Result<T> value) {
        *result = std::move(value);
        *completed = true;
        if (guard) guard->quit();
    });
    if (!*completed) { timer.start(4000); loop.exec(); }
    return *result;
}
wc::Result<wc::UserInfo> login(WarehouseClient& client, quint16 port, const QString& name) {
    const auto connection = awaitResult<int>([&](auto done) {
        client.connectToServer("127.0.0.1", port, [done](wc::Status status) {
            done({status, status.ok() ? std::optional<int>(1) : std::nullopt});
        });
    });
    if (!connection.status.ok()) return {connection.status, {}};
    return awaitResult<wc::UserInfo>([&](auto done) { client.login(name, "fixture-password", done); });
}
QString key(const wc::UserInfo& user) { return QStringLiteral("network:%1").arg(user.userId); }
}

class CareSnapshotIntegrationTests : public QObject {
    Q_OBJECT
private slots:
    void authorizedWarehouseSourcesFeedIndependentCareAlgorithms() {
        QTemporaryDir directory;
        QString error;
        WarehouseServer server(llm_recommendation::Config{});
        QVERIFY2(server.openDatabase(directory.filePath("warehouse.sqlite"), error), qPrintable(error));
        for (const auto& name : {"alice", "bob", "carol"})
            QVERIFY2(server.addUser(name, "fixture-password", error), qPrintable(error));
        QVERIFY(server.listen(QHostAddress::LocalHost));
        WarehouseClient alice, bob, carol;
        const auto a = login(alice, server.serverPort(), "alice");
        const auto b = login(bob, server.serverPort(), "bob");
        const auto c = login(carol, server.serverPort(), "carol");
        QVERIFY(a.status.ok() && a.value && b.status.ok() && b.value && c.status.ok() && c.value);
        const auto mine = awaitResult<wc::WarehouseSummary>([&](auto done) {
            alice.createWarehouse("alice-private", wc::Visibility::Private, done);
        });
        const auto shared = awaitResult<wc::WarehouseSummary>([&](auto done) {
            alice.createWarehouse("shared", wc::Visibility::Public, done);
        });
        const auto other = awaitResult<wc::WarehouseSummary>([&](auto done) {
            bob.createWarehouse("bob-private", wc::Visibility::Private, done);
        });
        QVERIFY(mine.value && shared.value && other.value);
        MediaRecord music;
        music.id = "client-local-row"; // Must not serve as a canonical work ID.
        music.fileName = "Music";
        music.category = "music";
        auto film = music; film.fileName = "Film"; film.category = "film";
        const auto privateItem = awaitResult<wc::WarehouseItem>([&](auto done) {
            alice.addItem(mine.value->warehouseId, music, done);
        });
        const auto publicItem = awaitResult<wc::WarehouseItem>([&](auto done) {
            alice.addItem(shared.value->warehouseId, music, done);
        });
        const auto otherItem = awaitResult<wc::WarehouseItem>([&](auto done) {
            bob.addItem(other.value->warehouseId, film, done);
        });
        QVERIFY(privateItem.value && publicItem.value && otherItem.value);
        const auto forbidden = awaitResult<wc::WarehouseDetail>([&](auto done) {
            bob.getWarehouse(mine.value->warehouseId, done);
        });
        QCOMPARE(forbidden.status.code, wc::ErrorCode::PermissionDenied);
        // The fixture models a SERVER identity registry. R05 must persist this
        // mapping, not trust a client's CSV row ID or merge people by name.
        QMap<QPair<qint64, qint64>, QString> identities;
        identities[{mine.value->warehouseId, privateItem.value->itemId}] = "work:music";
        identities[{shared.value->warehouseId, publicItem.value->itemId}] = "work:music";
        identities[{other.value->warehouseId, otherItem.value->itemId}] = "work:film";
        const auto resources = [&](const QList<wc::WarehouseDetail>& readable, bool publicOnly) {
            QList<cc::ResourceSummary> output;
            for (const auto& warehouse : readable) {
                if (publicOnly && warehouse.warehouse.visibility != wc::Visibility::Public) continue;
                for (const auto& item : warehouse.items) {
                    const auto location = qMakePair(warehouse.warehouse.warehouseId, item.itemId);
                    const auto workId = identities.value(location);
                    if (workId.isEmpty()) continue; // Test fixture registry, not production fallback.
                    output.append({{workId, {location.first, location.second}}, item.media,
                                   {{"person:confirmed-1", "Known musician", {cc::PersonRole::Musician}, false}}});
                }
            }
            return output;
        };
        const auto visibleA = server.readableCandidates(a.value->userId);
        const auto visibleB = server.readableCandidates(b.value->userId);
        QVERIFY(visibleA && visibleB);
        const auto worksA = resources(*visibleA, false);
        const auto worksB = resources(*visibleB, false);
        const auto publicWorks = resources(*visibleA, true);
        QCOMPARE(worksA.size(), 2);
        QCOMPARE(worksB.size(), 2);
        QCOMPARE(publicWorks.size(), 1);
        constexpr qint64 Now = 1794672000;
        QList<ca::ObservedBehavior> events;
        const auto view = [&](const QString& id, const QString& user, const cc::ResourceSummary& work) {
            ca::ObservedBehavior event;
            event.userKey = user;
            event.record = {{id, cc::BehaviorAction::View, work.resource, {}}, Now - 60, true};
            event.workSnapshot = work;
            event.publicEligible = true;
            return event;
        };
        for (const auto& user : {*a.value, *b.value, *c.value})
            events.append(view("public-view", key(user), publicWorks.first()));
        cc::ResourceSummary privateWork;
        for (const auto& work : worksA)
            if (work.resource.source.warehouseId == mine.value->warehouseId) privateWork = work;
        for (int i = 0; i < 4; ++i)
            events.append(view(QStringLiteral("private-%1").arg(i), key(*a.value), privateWork));
        events.append(events.first()); // A retry must not inflate either report.
        ca::PersonalSnapshot personal;
        personal.userKey = key(*a.value);
        personal.nowUtc = Now;
        personal.events = events;
        personal.readableWorks = worksA;
        const auto footprint = ca::buildFootprint({{Now - 86400, Now}, {}}, personal);
        QVERIFY(footprint.status.ok() && footprint.value);
        QCOMPARE(footprint.value->current.views, qint64(5));
        QCOMPARE(footprint.value->current.uniqueWorks, qint64(1));
        QVERIFY(footprint.value->containsSyntheticData);
        const auto trending = ca::buildTrending({cc::TrendingKind::Works, {Now - 86400, Now}, 10},
                                              {Now, true, events, publicWorks});
        QVERIFY(trending.status.ok() && trending.value);
        QCOMPARE(trending.value->items.size(), 1);
        QCOMPARE(trending.value->items.first().distinctUsers, qint64(3));
        QCOMPARE(trending.value->items.first().effectiveEvents, qint64(3));
        const auto people = ca::searchPeople({"musician", {}}, {true, worksA, {}});
        QVERIFY(people.status.ok() && people.value);
        QCOMPARE(people.value->items.size(), 1);
        personal.publicHeat["work:music"] = 3;
        personal.publicHeatContainsSyntheticData = true;
        const auto recommendation = ca::recommendTemporal({}, personal);
        QVERIFY(recommendation.status.ok() && recommendation.value);
        QCOMPARE(recommendation.value->items.size(), 1);
        QCOMPARE(recommendation.value->items.first().work.resource.workId, QStringLiteral("work:music"));
        QVERIFY(recommendation.value->items.first().work.media.id != QStringLiteral("work:music"));
        personal.notInterestedWorkIds.insert("work:music");
        const auto excluded = ca::recommendTemporal({}, personal);
        QVERIFY(excluded.status.ok() && excluded.value);
        QVERIFY(excluded.value->items.isEmpty()); // Both canonical sources excluded.
        personal.historyAvailable = false;
        QVERIFY(!ca::recommendTemporal({}, personal).status.ok());
        const auto removed = awaitResult<int>([&](auto done) {
            alice.removeItem(shared.value->warehouseId, publicItem.value->itemId, [done](wc::Status status) {
                done({status, status.ok() ? std::optional<int>(1) : std::nullopt});
            });
        });
        QVERIFY(removed.status.ok());
        const auto afterRemoval = server.readableCandidates(b.value->userId);
        QVERIFY(afterRemoval);
        const auto currentPublic = resources(*afterRemoval, true);
        QVERIFY(currentPublic.isEmpty());
        const auto expiredTrend = ca::buildTrending({cc::TrendingKind::Works, {Now - 86400, Now}, 10},
                                                   {Now, true, events, currentPublic});
        QVERIFY(expiredTrend.status.ok() && expiredTrend.value);
        QVERIFY(expiredTrend.value->items.isEmpty()); // Historical source is no longer public.
    }
};
QTEST_GUILESS_MAIN(CareSnapshotIntegrationTests)
#include "care_snapshot_integration_tests.moc"
