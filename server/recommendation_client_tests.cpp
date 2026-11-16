#include "warehouse_client.h"
#include "warehouse_server.h"
#include <QTemporaryDir>
#include <QtTest>
using namespace warehouse_contract;
class Integration : public QObject {
 Q_OBJECT
private slots:
 void testFlow() {
  QTemporaryDir dir; QString error; WarehouseServer server(llm_recommendation::Config{});
  QVERIFY(server.openDatabase(dir.filePath("server.sqlite"),error));
  QVERIFY(server.addUser("alice","alice-demo",error)); QVERIFY(server.listen(QHostAddress::LocalHost));
  WarehouseClient client; bool done=false; Status status;
  client.connectToServer("127.0.0.1",server.serverPort(),[&](Status s){status=s;done=true;});
  QTRY_VERIFY(done); QVERIFY(status.ok()); done=false;
  client.login("alice","alice-demo",[&](auto r){status=r.status;done=true;}); QTRY_VERIFY(done); QVERIFY(status.ok());
  Result<WarehouseSummary> warehouse; done=false;
  client.createWarehouse("test",Visibility::Private,[&](auto r){warehouse=r;done=true;}); QTRY_VERIFY(done); QVERIFY(warehouse.status.ok());
  MediaRecord media; media.id="fixture:1"; media.fileName="Music"; media.category="music";
  Result<WarehouseItem> item; done=false;
  client.addItem(warehouse.value->warehouseId,media,[&](auto r){item=r;done=true;}); QTRY_VERIFY(done); QVERIFY(item.status.ok());
  Result<WarehouseRecommendationResult> result; done=false;
  client.recommendResources({"Music",{},5},[&](auto r){result=r;done=true;}); QTRY_VERIFY(done); QVERIFY(result.status.ok());
  QVERIFY(result.value->fallback); QCOMPARE(result.value->items.size(),1); QCOMPARE(result.value->items.first().itemId,item.value->itemId);
  done=false; int calls=0;
  const auto handle=client.recommendResources({"Music",{},5},[&](auto r){status=r.status;done=true;++calls;});
  QVERIFY(client.cancelRecommendation(handle)); QTRY_VERIFY(done); QCOMPARE(status.code,ErrorCode::Cancelled);
  QTest::qWait(100); QCOMPARE(calls,1);
 }
};
QTEST_GUILESS_MAIN(Integration)
#include "recommendation_client_tests.moc"
