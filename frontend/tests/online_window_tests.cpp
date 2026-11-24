#include "../online_window.h"
#include "../warehouse_page.h"
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QHash>
#include <QtTest>
#include <algorithm>
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
    bool defer = false, removed = false;
    bool metadataVariants = false;
    bool denyPublic = false, deferTemporal = false;
    QStringList cancelled;
    Callback<TemporalRecommendationResult> temporalDone;
    QList<BrowseScene> requestedScenes;
    TemporalRecommendationResult temporalResult;
    WarehouseRecommendationResult warehouseResult;
    QHash<qint64, QList<WarehouseItem>> catalogItems;
    WarehouseSummary mine{1, "Mine", 7, Visibility::Private};
    WarehouseSummary shared{2, "Shared", 8, Visibility::Public};
    MediaRecord media(QString title) {
        MediaRecord m;
        m.id="duplicate"; m.fileName=title; m.director="Director"; m.fileType="MP4";
        if (metadataVariants && title == "Private movie") {
            m.fileType.clear();
            m.releasePeriod = "1981";
        } else if (metadataVariants) {
            m.releaseDate = QDate(1981, 5, 22);
        }
        return m;
    }
    void connectToServer(const QString&, quint16, StatusCallback cb) override { ++connects; cb({}); }
    void disconnectFromServer() override { ++disconnects; }
    void login(const QString&, const QString&, Callback<UserInfo> cb) override { ++logins; cb(ok(UserInfo{7,"reader"})); }
    void logout(StatusCallback cb) override { cb({}); }
    void listMyWarehouses(Callback<QList<WarehouseSummary>> cb) override { cb(ok(QList<WarehouseSummary>{mine})); }
    void listPublicWarehouses(Callback<QList<WarehouseSummary>> cb) override { cb(ok(QList<WarehouseSummary>{shared})); }
    void getWarehouse(qint64 id, Callback<WarehouseDetail> cb) override {
        if (defer) { delayed=std::move(cb); return; }
        if (id==2 && denyPublic) { cb({{warehouse_contract::ErrorCode::PermissionDenied,"Denied"}, {}}); return; }
        QList<WarehouseItem> items;
        if (catalogItems.contains(id)) items = catalogItems.value(id);
        else if (id != 1 || !removed) items.append({11,media(id==1?"Private movie":"Public movie")});
        cb(ok(WarehouseDetail{id==1?mine:shared, items}));
    }
    void createWarehouse(const QString&, Visibility, Callback<WarehouseSummary> cb) override { cb(ok(mine)); }
    void addItem(qint64, const MediaRecord&, Callback<WarehouseItem> cb) override { cb(ok(WarehouseItem{})); }
    void removeItem(qint64,qint64,StatusCallback cb) override { removed = true; cb({}); }
    RecommendationHandle recommendResources(const WarehouseRecommendationRequest&, Callback<WarehouseRecommendationResult> cb) override { cb(ok(warehouseResult)); return {}; }
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
    RecommendationHandle recommendTemporal(const TemporalRecommendationRequest& request,Callback<TemporalRecommendationResult> cb) override {
        requestedScenes.append(request.scene);
        ++temporalCalls;
        if (deferTemporal) { temporalDone=std::move(cb); return "today-pending"; }
        cb(ok(temporalResult)); return {};
    }
};
}
class OnlineWindowTests final : public QObject {
    Q_OBJECT
private slots:
    void catalogAndSearchGroupTypesAndKeepReleaseOrderAfterFilterAndRefresh()
    {
        FakeService service;
        auto add = [&](qint64 warehouse, qint64 id, const QString& name, const QString& type, const QString& date) {
            auto media = service.media(name); media.fileType = type;
            if (!parseMediaRelease(date, media)) media.releasePeriod = date; // Defensive malformed-date case.
            service.catalogItems[warehouse].append({id, media});
        };
        // Deliberately scrambled, across two warehouses, with IDs unrelated to dates.
        add(2, 11, "MP4 unknown", "MP4", "");
        add(1, 61, "MP4 month", " mp4 ", "2025-06");
        add(2, 9, "MP4 old", "MP4", "2024-12-31");
        add(1, 50, "MP3 old", "mp3", "1990");
        add(2, 90, "MP4 year", "MP4", "2025");
        add(1, 51, "MP4 January", "MP4", "2025-01-01");
        add(2, 35, "MP4 same B", "MP4", "2025-06-01");
        add(1, 71, "MP4 same A", "MP4", "2025-06-01");
        add(2, 3, "MP4 same source", "MP4", "2025-06-01");
        add(1, 70, "MP4 same source", "MP4", "2025-06-01");
        add(1, 40, "FLAC unknown", "FLAC", "");
        add(2, 10, "MP4 next year", "MP4", "2026");
        add(1, 150, "Unknown type", "", "2029-12-31");
        add(2, 12, "MP4 late day", "MP4", "2025-06-30");
        add(1, 30, "MP4 invalid", "MP4", "2025-13");
        const QStringList expected{"FLAC unknown", "MP3 old", "MP4 next year", "MP4 late day",
            "MP4 same A", "MP4 same B", "MP4 same source", "MP4 same source", "MP4 month",
            "MP4 January", "MP4 year", "MP4 old", "MP4 invalid", "MP4 unknown", "Unknown type"};
        auto titles = [](QTableWidget* table) {
            QStringList result;
            for (int row = 0; row < table->rowCount(); ++row) result.append(table->item(row, 0)->text());
            return result;
        };
        std::unique_ptr<QWidget> window(createOnlineWindow(service, service, {7,"reader"}, [] {}));
        auto* catalog = window->findChild<QTableWidget*>("onlineMediaList");
        auto* results = window->findChild<QTableWidget*>("onlineSearchList");
        auto* mediaType = window->findChild<QComboBox*>("onlineMediaType");
        auto* searchType = window->findChild<QComboBox*>("onlineSearchType");
        auto* release = window->findChild<QLineEdit*>("onlineSearchRelease");
        auto* search = window->findChild<QPushButton*>("onlineSearchSubmit");
        QTRY_COMPARE(catalog->rowCount(), expected.size());
        QCOMPARE(titles(catalog), expected);
        QCOMPARE(catalog->item(6, 0)->data(Qt::UserRole).toString(), QString("1/70"));
        QCOMPARE(catalog->item(7, 0)->data(Qt::UserRole).toString(), QString("2/3"));
        QCOMPARE(catalog->item(8, 4)->text(), QString("2025-06"));
        QCOMPARE(catalog->item(8, 2)->text(), QString("MP4"));
        QCOMPARE(catalog->item(10, 4)->text(), QString("2025"));
        QVERIFY(service.events.isEmpty());
        mediaType->setCurrentIndex(mediaType->findText("MP4"));
        QCOMPARE(titles(catalog), expected.mid(2, 12));
        window->findChild<QLineEdit*>("onlineSearchDirector")->setText("Director");
        search->click(); QCOMPARE(titles(results), expected); // Catalog type filter is independent.
        searchType->setCurrentIndex(searchType->findText("MP4"));
        search->click(); QCOMPARE(titles(results), expected.mid(2, 12));
        release->setText("2025"); search->click();
        const auto yearMatches = expected.mid(3, 8);
        QCOMPARE(titles(results), yearMatches);
        QCOMPARE(service.events.size(), 3);
        release->setText("1900"); // Unsubmitted edits must not replace the last applied search on refresh.
        for (auto it = service.catalogItems.begin(); it != service.catalogItems.end(); ++it)
            std::reverse(it->begin(), it->end());
        add(1, 99, "MP4 new after refresh", "MP4", "2027-02-01");
        window->findChild<QPushButton*>("onlineMediaRefresh")->click();
        QTRY_COMPARE(catalog->rowCount(), 13);
        QCOMPARE(mediaType->currentText(), QString("MP4"));
        auto updated = expected.mid(2, 12); updated.prepend("MP4 new after refresh");
        QCOMPARE(titles(catalog), updated);
        QCOMPARE(titles(results), yearMatches);
        QCOMPARE(service.events.size(), 3); // Refresh/filter/automatic selection do not record behavior.
        release->clear(); search->click(); QCOMPARE(titles(results), updated);
        searchType->setCurrentIndex(1); search->click();
        QCOMPARE(titles(results), QStringList{"Unknown type"});
        mediaType->setCurrentIndex(1);
        QCOMPARE(titles(catalog), QStringList{"Unknown type"});
        const auto eventCount = service.events.size();
        catalog->cellClicked(0, 0);
        QCOMPARE(service.opened.last().warehouseId, qint64(1));
        QCOMPARE(service.opened.last().itemId, qint64(150));
        QCOMPARE(service.events.size(), eventCount + 1);
    }

    void recommendationPagesPreserveServiceOrderInsteadOfCatalogOrder()
    {
        FakeService service;
        QStringList returned;
        for (int index = 0; index < 8; ++index) {
            ResourceSummary work;
            work.resource = {QString::number(index), {2, index + 1}};
            work.media = service.media(QString("Rank %1").arg(8 - index));
            work.media.fileType = index % 2 == 0 ? "MP4" : "FLAC";
            work.media.releaseDate = QDate(1990 + index, 1, 1);
            service.temporalResult.items.append({work, "reason"});
            service.warehouseResult.items.append({2, index + 1, work.media, "reason"});
            returned.append(work.media.fileName);
        }
        std::unique_ptr<QWidget> window(createOnlineWindow(service, service, {7,"reader"}, [] {}));
        auto* catalog = window->findChild<QTableWidget*>("onlineMediaList");
        QTRY_COMPARE(catalog->rowCount(), 2);
        auto* tabs = window->findChild<QTabWidget*>("onlineTabs");
        tabs->setCurrentIndex(2);
        auto* today = window->findChild<QListWidget*>("onlineTodayList");
        QCOMPARE(today->count(), 6);
        for (int row = 0; row < today->count(); ++row) QCOMPARE(today->item(row)->text(), returned[row]);
        window->findChild<QComboBox*>("onlineScene")->setCurrentIndex(1);
        QCOMPARE(today->count(), 6);
        for (int row = 0; row < today->count(); ++row) QCOMPARE(today->item(row)->text(), returned[row]);
        tabs->setCurrentIndex(6);
        auto* temporal = window->findChild<QListWidget*>("careTemporalList");
        QCOMPARE(temporal->count(), returned.size());
        for (int row = 0; row < temporal->count(); ++row) QCOMPARE(temporal->item(row)->text(), returned[row]);
        tabs->setCurrentIndex(3);
        window->findChild<QLineEdit*>("warehouseRecommendationQuery")->setText("test query");
        auto* page = tabs->widget(3);
        QPushButton* recommend = nullptr;
        for (auto* button : page->findChildren<QPushButton*>())
            if (button->text() == QStringLiteral("获取推荐")) recommend = button;
        QVERIFY(recommend); QVERIFY(recommend->isEnabled()); recommend->click();
        auto* suggestions = window->findChild<QListWidget*>("onlineWarehouseRecommendations");
        QCOMPARE(suggestions->count(), returned.size());
        for (int row = 0; row < suggestions->count(); ++row) QVERIFY(suggestions->item(row)->text().startsWith(returned[row]));
    }
    void sceneChangesReloadTodayAndCancelOldScene() {
        FakeService service;
        std::unique_ptr<QWidget> window(createOnlineWindow(service,service,{7,"reader"},[]{}));
        window->findChild<QTabWidget*>("onlineTabs")->setCurrentIndex(2);
        auto* scene=window->findChild<QComboBox*>("onlineScene");QVERIFY(scene);
        scene->setCurrentIndex(1);QCOMPARE(service.requestedScenes.last(),BrowseScene::Relax);
        service.deferTemporal=true;scene->setCurrentIndex(2);
        auto late=service.temporalDone;
        scene->setCurrentIndex(3);
        QCOMPARE(service.requestedScenes.last(),BrowseScene::Explore);
        QVERIFY(service.cancelled.contains("today-pending"));
        TemporalRecommendationResult stale;RecommendedResource resource;resource.work.media.fileName="OLD";resource.work.resource.workId="old";
        stale.items.append(resource);late(ok(stale));
        QCOMPARE(window->findChild<QListWidget*>("onlineTodayList")->count(),0);
    }
    void searchCombinesReleasePrecisionAndActualFileType()
    {
        FakeService service;
        service.metadataVariants = true;
        std::unique_ptr<QWidget> window(createOnlineWindow(service, service, {7,"reader"}, [] {}));
        auto* catalog = window->findChild<QTableWidget*>("onlineMediaList");
        QTRY_COMPARE(catalog->rowCount(), 2);
        QCOMPARE(catalog->item(1, 4)->text(), QString("1981"));

        auto* type = window->findChild<QComboBox*>("onlineSearchType");
        auto* release = window->findChild<QLineEdit*>("onlineSearchRelease");
        auto* submit = window->findChild<QPushButton*>("onlineSearchSubmit");
        auto* results = window->findChild<QTableWidget*>("onlineSearchList");
        release->setText("1981");
        submit->click();
        QCOMPARE(results->rowCount(), 2);
        const auto mp4 = type->findText("MP4");
        QVERIFY(mp4 >= 0);
        type->setCurrentIndex(mp4);
        submit->click();
        QCOMPARE(results->rowCount(), 1);
        QCOMPARE(results->item(0, 4)->text(), QString("1981-05-22"));
        type->setCurrentIndex(0);
        release->setText("1981-05");
        submit->click();
        QCOMPARE(results->rowCount(), 1);
        type->setCurrentIndex(1);
        submit->click();
        QCOMPARE(results->rowCount(), 0);
        release->setText("1981");
        submit->click();
        QCOMPARE(results->rowCount(), 1);
        QCOMPARE(results->item(0, 4)->text(), QString("1981"));
    }
    void todayOnlyShowsTitlesAndUserFacingNotices() {
        FakeService service;
        ResourceSummary work;
        work.resource = {"canonical", {1, 11}};
        work.media = service.media("Public movie");
        service.temporalResult.items.append({work, QStringLiteral("近期兴趣权重 0.6，总分 0.9")});
        service.temporalResult.message = QStringLiteral("按内部权重排序");
        service.temporalResult.containsSyntheticData = true;
        std::unique_ptr<QWidget> window(createOnlineWindow(service,service,{7,"reader"},[]{}));
        window->findChild<QTabWidget*>("onlineTabs")->setCurrentIndex(2);
        auto* list=window->findChild<QListWidget*>("onlineTodayList");
        QCOMPARE(list->count(), 1);
        QCOMPARE(list->item(0)->text(), QStringLiteral("Public movie"));
        QCOMPARE(window->findChild<QLabel*>("onlineTodayStatus")->text(), QStringLiteral("含模拟数据"));
    }
    void searchWaitsForCatalog() {
        FakeService service; service.defer = true;
        std::unique_ptr<QWidget> window(createOnlineWindow(service,service,{7,"reader"},[]{}));
        QTRY_VERIFY(bool(service.delayed));
        window->findChild<QLineEdit*>("onlineSearchDirector")->setText("Director");
        window->findChild<QPushButton*>("onlineSearchSubmit")->click();
        QVERIFY(service.events.isEmpty());
    }
    void deleteRefreshesCatalogAndSearch() {
        FakeService service;
        std::unique_ptr<QWidget> window(createOnlineWindow(service,service,{7,"reader"},[]{}));
        auto* media=window->findChild<QTableWidget*>("onlineMediaList");
        QTRY_COMPARE(media->rowCount(),2);
        auto* tabs=window->findChild<QTabWidget*>("onlineTabs");
        tabs->setCurrentIndex(3);
        auto* items=window->findChild<QListWidget*>("onlineWarehouseItems");
        QCOMPARE(items->count(),1);
        items->setCurrentRow(0);
        auto buttons=window->findChildren<QPushButton*>();
        auto remove=std::find_if(buttons.begin(),buttons.end(),[](auto* b) { return b->text()==QStringLiteral("删除条目"); });
        QVERIFY(remove!=buttons.end());
        QTimer::singleShot(0, [] {
            if (auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                box->button(QMessageBox::Yes)->click();
        });
        (*remove)->click();
        QVERIFY(service.removed);
        QCOMPARE(items->count(),0);
        tabs->setCurrentIndex(0);
        QTRY_COMPARE(media->rowCount(),1);
        tabs->setCurrentIndex(1);
        window->findChild<QLineEdit*>("onlineSearchTitle")->setText("Private movie");
        window->findChild<QPushButton*>("onlineSearchSubmit")->click();
        QCOMPARE(window->findChild<QTableWidget*>("onlineSearchList")->rowCount(),0);
    }
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
        auto* catalog=window->findChild<QTableWidget*>("onlineMediaList");
        QTRY_COMPARE(catalog->rowCount(),2);
        QCOMPARE(tabs->count(),8);
        QVERIFY(service.events.isEmpty());
        tabs->setCurrentIndex(1); tabs->setCurrentIndex(0);
        QVERIFY(service.events.isEmpty());
        QVERIFY(!window->findChild<QLabel*>("onlineMediaDetail"));
        QCOMPARE(catalog->columnCount(),5);
        QCOMPARE(catalog->item(0, 4)->text(),QStringLiteral("—"));
        catalog->setCurrentCell(0, 0);
        QVERIFY(service.events.isEmpty());
        catalog->cellClicked(0, 2);
        QCOMPARE(service.opened.size(),1);
        QCOMPARE(service.opened.front().warehouseId,1);
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
        QCOMPARE(window->findChild<QTableWidget*>("onlineSearchList")->rowCount(),2);
        QCOMPARE(service.opened.size(),1);
    }
    void permissionFailuresDoNotEnterTheIndex() {
        FakeService service; service.denyPublic=true;
        std::unique_ptr<QWidget> window(createOnlineWindow(service,service,{7,"reader"},[]{}));
        auto* catalog=window->findChild<QTableWidget*>("onlineMediaList");
        QTRY_COMPARE(catalog->rowCount(),1);
        QVERIFY(catalog->item(0, 0)->text().contains("Private movie"));
        window->findChild<QLineEdit*>("onlineSearchDirector")->setText("Director");
        window->findChild<QPushButton*>("onlineSearchSubmit")->click();
        QCOMPARE(window->findChild<QTableWidget*>("onlineSearchList")->rowCount(),1);
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
        QCOMPARE(window->findChild<QTableWidget*>("onlineMediaList")->rowCount(),0);
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
