#include "../online_window.h"
#include "../warehouse_page.h"
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTabWidget>
#include <QtTest>
#include <memory>

using warehouse_contract::WarehouseService;
using warehouse_contract::UserInfo;
using warehouse_contract::WarehouseSummary;
using warehouse_contract::WarehouseDetail;
using warehouse_contract::WarehouseItem;
using warehouse_contract::Visibility;
using warehouse_contract::RecommendationHandle;
using warehouse_contract::WarehouseRecommendationRequest;
using warehouse_contract::WarehouseRecommendationResult;
using namespace care_contract;
namespace {
template<class T> Result<T> ok(T value) { return {{}, std::move(value)}; }
class FakeService final : public WarehouseService, public CareService {
public:
    int connects = 0, logins = 0, disconnects = 0, temporalCalls = 0;
    QList<BehaviorEvent> events;
    QList<WarehouseItemRef> opened;
    Callback<WarehouseDetail> delayed;
    bool defer = false;
    bool denyPublic = false, deferTemporal = false;
    QStringList cancelled;
    Callback<TemporalRecommendationResult> temporalDone;
    WarehouseSummary mine{1, "Mine", 7, Visibility::Private};
    WarehouseSummary shared{2, "Shared", 8, Visibility::Public};
    MediaRecord media(QString title) { MediaRecord m; m.id="duplicate"; m.fileName=title; m.director="Director"; m.fileType="MP4"; return m; }
    void connectToServer(const QString&, quint16, StatusCallback cb) override { ++connects; cb({}); }
    void disconnectFromServer() override { ++disconnects; }
    void login(const QString&, const QString&, Callback<UserInfo> cb) override { ++logins; cb(ok(UserInfo{7,"reader"})); }
    void logout(StatusCallback cb) override { cb({}); }
    void listMyWarehouses(Callback<QList<WarehouseSummary>> cb) override { cb(ok(QList<WarehouseSummary>{mine})); }
    void listPublicWarehouses(Callback<QList<WarehouseSummary>> cb) override { cb(ok(QList<WarehouseSummary>{shared})); }
    void getWarehouse(qint64 id, Callback<WarehouseDetail> cb) override {
        if (defer) { delayed=std::move(cb); return; }
        if (id==2 && denyPublic) { cb({{warehouse_contract::ErrorCode::PermissionDenied,"Denied"}, {}}); return; }
        cb(ok(WarehouseDetail{id==1?mine:shared, {{11,media(id==1?"Private movie":"Public movie")}}}));
    }
    void createWarehouse(const QString&, Visibility, Callback<WarehouseSummary> cb) override { cb(ok(mine)); }
    void addItem(qint64, const MediaRecord&, Callback<WarehouseItem> cb) override { cb(ok(WarehouseItem{})); }
    void removeItem(qint64,qint64,StatusCallback cb) override { cb({}); }
    RecommendationHandle recommendResources(const WarehouseRecommendationRequest&, Callback<WarehouseRecommendationResult> cb) override { cb(ok(WarehouseRecommendationResult{})); return {}; }
    bool cancelRecommendation(const RecommendationHandle& handle) override {
        cancelled.append(handle);
        auto cb=std::exchange(temporalDone,{});
        if (cb) cb({{warehouse_contract::ErrorCode::Cancelled,"Cancelled"},{}});
        return bool(cb);
    }
    void recordBehavior(const BehaviorEvent& e, StatusCallback cb) override { events.append(e); cb({}); }
    void getResource(const WarehouseItemRef& source, Callback<ResourceSummary> cb) override {
        opened.append(source); ResourceSummary r; r.resource={"canonical",source}; r.media=media("Opened"); cb(ok(r));
    }
    void getParticipationSettings(Callback<ParticipationSettings> cb) override { cb(ok(ParticipationSettings{})); }
    void setParticipationSettings(const ParticipationSettings&,StatusCallback cb) override { cb({}); }
    void clearHistory(StatusCallback cb) override { cb({}); }
    void searchPeople(const PersonSearchRequest&,Callback<Page<PersonSummary>> cb) override { cb(ok(Page<PersonSummary>{})); }
    void listWorkFeedback(const PageRequest&,Callback<Page<WorkFeedback>> cb) override { cb(ok(Page<WorkFeedback>{})); }
    void setWorkFeedback(const WorkFeedbackRequest&,StatusCallback cb) override { cb({}); }
    void listFollows(const PageRequest&,Callback<Page<PersonSummary>> cb) override { cb(ok(Page<PersonSummary>{})); }
    void setFollow(const QString&,bool,StatusCallback cb) override { cb({}); }
    void getPersonWorks(const QString&,const PageRequest&,Callback<Page<ResourceSummary>> cb) override { cb(ok(Page<ResourceSummary>{})); }
    void getPersonUpdates(const PersonUpdatesRequest&,Callback<Page<PersonUpdate>> cb) override { cb(ok(Page<PersonUpdate>{})); }
    void getTrending(const TrendingRequest&,Callback<TrendingResult> cb) override { cb(ok(TrendingResult{})); }
    void getFootprint(const FootprintRequest&,Callback<FootprintReport> cb) override { cb(ok(FootprintReport{})); }
    RecommendationHandle recommendTemporal(const TemporalRecommendationRequest&,Callback<TemporalRecommendationResult> cb) override {
        ++temporalCalls;
        if (deferTemporal) { temporalDone=std::move(cb); return "today-pending"; }
        cb(ok(TemporalRecommendationResult{})); return {};
    }
};
}
class OnlineWindowTests final : public QObject {
    Q_OBJECT
private slots:
    void adoptedSessionDoesNotReconnectAndClearsOldRows() {
        FakeService service;
        WarehousePage page(service, [] { return QList<MediaRecord>{}; });
        page.adoptSession({7,"alice"});
        QCOMPARE(service.connects,0); QCOMPARE(service.logins,0); QCOMPARE(service.disconnects,0);
        QVERIFY(page.isSignedIn());
        QVERIFY(page.findChild<QLineEdit*>("warehousePassword")->isHidden());
        auto* list=page.findChild<QListWidget*>("onlineWarehouseList");
        QCOMPARE(list->count(),1);
        service.defer=true;
        page.adoptSession({9,"bob"});
        QCOMPARE(page.findChild<QListWidget*>("onlineWarehouseItems")->count(),0);
        page.connectionLost();
        service.delayed(ok(WarehouseDetail{service.mine,{{11,service.media("stale")}}}));
        QCOMPARE(page.findChild<QListWidget*>("onlineWarehouseItems")->count(),0);
    }
    void catalogAndSearchKeepRealSourcesAndOnlyExplicitActionsCount() {
        FakeService service;
        std::unique_ptr<QWidget> window(createOnlineWindow(service,service,{7,"reader"},[]{}));
        window->show();
        auto* tabs=window->findChild<QTabWidget*>("onlineTabs");
        auto* catalog=window->findChild<QListWidget*>("onlineMediaList");
        QTRY_COMPARE(catalog->count(),2);
        QCOMPARE(tabs->count(),8);
        QVERIFY(service.events.isEmpty());
        tabs->setCurrentIndex(1); tabs->setCurrentIndex(0);
        QVERIFY(service.events.isEmpty());
        catalog->itemClicked(catalog->item(0));
        QCOMPARE(service.opened.size(),1);
        QCOMPARE(service.opened.front().warehouseId,2);
        QCOMPARE(service.opened.front().itemId,11);
        QCOMPARE(service.events.size(),1);
        QCOMPARE(service.events.front().resource->workId,QString("canonical"));
        tabs->setCurrentIndex(1);
        window->findChild<QLineEdit*>("onlineSearchDirector")->setText("Director");
        window->findChild<QPushButton*>("onlineSearchSubmit")->click();
        QCOMPARE(service.events.size(),2);
        QCOMPARE(service.events.back().action,BehaviorAction::Search);
        QVERIFY(!service.events.back().resource);
        QVERIFY(!service.events.back().eventId.isEmpty());
        QCOMPARE(window->findChild<QListWidget*>("onlineSearchList")->count(),2);
        QCOMPARE(service.opened.size(),1);
    }
    void permissionFailuresDoNotEnterTheIndex() {
        FakeService service; service.denyPublic=true;
        std::unique_ptr<QWidget> window(createOnlineWindow(service,service,{7,"reader"},[]{}));
        auto* catalog=window->findChild<QListWidget*>("onlineMediaList");
        QTRY_COMPARE(catalog->count(),1);
        QVERIFY(catalog->item(0)->text().contains("Private movie"));
        window->findChild<QLineEdit*>("onlineSearchDirector")->setText("Director");
        window->findChild<QPushButton*>("onlineSearchSubmit")->click();
        QCOMPARE(window->findChild<QListWidget*>("onlineSearchList")->count(),1);
    }
    void todayRevalidatesAndLogoutIsSingleAndCancelsPendingWork() {
        FakeService service;
        int logouts=0;
        std::unique_ptr<QWidget> window(createOnlineWindow(service,service,{7,"reader"},[&]{ ++logouts; }));
        auto* tabs=window->findChild<QTabWidget*>("onlineTabs");
        tabs->setCurrentIndex(2);
        QCOMPARE(service.temporalCalls,1);
        tabs->setCurrentIndex(0); tabs->setCurrentIndex(2);
        QCOMPARE(service.temporalCalls,2);
        QVERIFY(service.events.isEmpty());
        window->findChild<QPushButton*>("onlineLogout")->click();
        window->close();
        QCOMPARE(logouts,1);
        QCOMPARE(window->findChild<QListWidget*>("onlineMediaList")->count(),0);
        FakeService pending; pending.deferTemporal=true;
        std::unique_ptr<QWidget> other(createOnlineWindow(pending,pending,{9,"second"},[]{}));
        other->findChild<QTabWidget*>("onlineTabs")->setCurrentIndex(2);
        other->findChild<QPushButton*>("onlineLogout")->click();
        QCOMPARE(pending.cancelled,QStringList{"today-pending"});
        QCOMPARE(other->findChild<QListWidget*>("onlineTodayList")->count(),0);
    }
};
QTEST_MAIN(OnlineWindowTests)
#include "online_window_tests.moc"
