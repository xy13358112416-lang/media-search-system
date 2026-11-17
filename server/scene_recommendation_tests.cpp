#include "llm_scene.h"
#include "warehouse_server.h"
#include "online_care_client.h"
#include <QtTest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QPointer>
#include <QTemporaryDir>
#include <QTimer>
#include <QTcpSocket>

using namespace warehouse_contract;
using namespace care_contract;
class FakeWeb final : public QTcpServer {
public:
    QString mode;
    int calls=0,delay=0;
    QJsonObject last;
    FakeWeb() {
        listen(QHostAddress::LocalHost);
        connect(this,&QTcpServer::newConnection,this,[this] {
            while(auto* socket=nextPendingConnection()) {
                auto bytes=std::make_shared<QByteArray>();auto served=std::make_shared<bool>(false);
                connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
                connect(socket,&QTcpSocket::readyRead,this,[this,socket,bytes,served] {
                    *bytes+=socket->readAll();if(*served) return;
                    const auto split=bytes->indexOf("\r\n\r\n");if(split<0) return;
                    int length=0;
                    for(const auto& line:bytes->left(split).split('\n'))
                        if(line.toLower().startsWith("content-length:")) length=line.mid(15).trimmed().toInt();
                    if(bytes->size()<split+4+length) return;
                    *served=true;++calls;
                    last=QJsonDocument::fromJson(bytes->mid(split+4,length)).object();
                    const auto input=QJsonDocument::fromJson(last.value("messages").toArray().last().toObject().value("content").toString().toUtf8()).object();
                    const auto candidates=input.value("candidates").toArray();
                    const bool focus=input.value("scene").toString().contains(QStringLiteral("专注"));
                    const auto id=candidates.at(focus?candidates.size()-1:0).toObject().value("id").toString();
                    QJsonArray ids{id};
                    if(mode=="unknown") ids=QJsonArray{"not-in-candidates"};
                    if(mode=="duplicate") ids.append(id);
                    if(mode=="empty") ids={};
                    QJsonObject message{{"content",QString::fromUtf8(QJsonDocument(QJsonObject{{"ids",ids}}).toJson(QJsonDocument::Compact))}};
                    if(mode!="no_search") message.insert("server_tool_calls",QJsonArray{QJsonObject{{"calls",QJsonArray{
                        QJsonObject{{"type","deepseek_web_search"},{"status","completed"},{"kind","search"},
                                    {"sources",QJsonArray{QJsonObject{{"url","https://example.org/work"}}}}}}}}});
                    auto body=QJsonDocument(QJsonObject{{"choices",QJsonArray{QJsonObject{{"finish_reason","stop"},{"message",message}}}}}).toJson(QJsonDocument::Compact);
                    if(mode=="provider_error") body="{\"error\":{\"message\":\"failure\"}}";
                    if(mode=="oversized") body=QByteArray(600*1024,'x');
                    QPointer<QTcpSocket> guarded(socket);
                    QTimer::singleShot(delay,this,[guarded,body] {
                        if(!guarded || guarded->state()!=QAbstractSocket::ConnectedState) return;
                        guarded->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "+QByteArray::number(body.size())+"\r\n\r\n"+body);
                        guarded->disconnectFromHost();
                    });
                });
            }
        });
    }
    llm_recommendation::Config config() const {
        llm_recommendation::Config c;c.apiKey="fake-test-key";c.endpoint=QUrl(QString("http://127.0.0.1:%1/chat/completions").arg(serverPort()));c.timeoutMs=2000;return c;
    }
};
class SceneTests final : public QObject {
    Q_OBJECT
    QList<llm_recommendation::Candidate> candidates() {
        MediaRecord a;a.id="a";a.fileName="Gentle film";
        MediaRecord b;b.id="b";b.fileName="Background music";
        return {{"a",a},{"b",b}};
    }
private slots:
    void liveSearchIsRequiredAndModelIdsAreBounded() {
        FakeWeb web;llm_scene::Client client(web.config());
        bool done=false;llm_scene::Result result;
        client.recommend(BrowseScene::Relax,candidates(),6,[&](auto r){result=r;done=true;});
        QTRY_VERIFY(done);QVERIFY(!result.fallback);QCOMPARE(result.ids,QStringList{"a"});
        QVERIFY(!web.last.contains("response_format"));QCOMPARE(web.last.value("model").toString(),QString("deepseek-flash"));
        QCOMPARE(web.last.value("tools").toArray().first().toObject().value("type").toString(),QString("deepseek_web_search"));
        done=false;client.recommend(BrowseScene::Focus,candidates(),6,[&](auto r){result=r;done=true;});
        QTRY_VERIFY(done);QCOMPARE(result.ids,QStringList{"b"});
        for(const auto& mode:QStringList{"unknown","duplicate","no_search","provider_error","oversized"}) {
            web.mode=mode;done=false;
            client.recommend(BrowseScene::Relax,candidates(),6,[&](auto r){result=r;done=true;});
            QTRY_VERIFY(done);QVERIFY2(result.fallback,qPrintable(mode));
        }
        web.mode="empty";done=false;
        client.recommend(BrowseScene::Relax,candidates(),6,[&](auto r){result=r;done=true;});
        QTRY_VERIFY(done);QVERIFY(!result.fallback);QVERIFY(result.ids.isEmpty());
    }
    void noKeyTimeoutAndCancellation() {
        FakeWeb web;auto config=web.config();config.apiKey.clear();
        llm_scene::Client noKey(config);bool done=false;
        noKey.recommend(BrowseScene::Focus,candidates(),6,[&](auto r){QVERIFY(r.fallback);done=true;});
        QTRY_VERIFY(done);QCOMPARE(web.calls,0);
        config=web.config();config.timeoutMs=100;web.delay=400;
        llm_scene::Client slow(config);done=false;int callbacks=0;
        slow.recommend(BrowseScene::Focus,candidates(),6,[&](auto r){QVERIFY(r.fallback);done=true;++callbacks;});
        QTRY_VERIFY(done);QTest::qWait(450);QCOMPARE(callbacks,1);
        const auto id=slow.recommend(BrowseScene::Focus,candidates(),6,[&](auto){++callbacks;});
        slow.cancel(id);QTest::qWait(450);QCOMPARE(callbacks,1);
    }
    void realServerRechecksDeletionAndKeepsDefaultOffline() {
        FakeWeb web;QTemporaryDir dir;QString error;WarehouseServer server(web.config());
        QVERIFY(server.openDatabase(dir.filePath("scene.sqlite"),error));
        QVERIFY(server.addUser("alice","alice-demo",error));QVERIFY(server.listen(QHostAddress::LocalHost));
        OnlineCareClient client;bool done=false;Status status;
        client.connectToServer("127.0.0.1",server.serverPort(),[&](auto s){status=s;done=true;});QTRY_VERIFY(done);QVERIFY(status.ok());
        done=false;client.login("alice","alice-demo",[&](auto r){status=r.status;done=true;});QTRY_VERIFY(done);QVERIFY(status.ok());
        warehouse_contract::Result<WarehouseSummary> warehouse;done=false;
        client.createWarehouse("mine",Visibility::Private,[&](auto r){warehouse=r;done=true;});QTRY_VERIFY(done);QVERIFY(warehouse.value);
        MediaRecord media;media.id="movie";media.fileName="Public title";
        warehouse_contract::Result<WarehouseItem> item;done=false;
        client.addItem(warehouse.value->warehouseId,media,[&](auto r){item=r;done=true;});QTRY_VERIFY(done);QVERIFY(item.value);
        warehouse_contract::Result<TemporalRecommendationResult> result;done=false;
        client.recommendTemporal({},[&](auto r){result=r;done=true;});QTRY_VERIFY(done);QVERIFY(result.value);QCOMPARE(web.calls,0);
        TemporalRecommendationRequest request;request.scene=BrowseScene::Relax;request.limit=6;
        done=false;client.recommendTemporal(request,[&](auto r){result=r;done=true;});
        QTRY_VERIFY(done);QVERIFY(result.value);QVERIFY(!result.value->fallback);QCOMPARE(result.value->items.size(),1);
        QVERIFY(result.value->items.first().reason.isEmpty());
        web.delay=500;done=false;
        client.recommendTemporal(request,[&](auto r){result=r;done=true;});
        QTRY_COMPARE(web.calls,2);
        bool removed=false;client.removeItem(warehouse.value->warehouseId,item.value->itemId,[&](auto s){status=s;removed=true;});
        QTRY_VERIFY(removed);QVERIFY(status.ok());QTRY_VERIFY(done);QVERIFY(result.value);QVERIFY(result.value->items.isEmpty());
        done=false;client.addItem(warehouse.value->warehouseId,media,[&](auto r){item=r;done=true;});QTRY_VERIFY(done);QVERIFY(item.value);
        done=false;int completions=0;
        auto handle=client.recommendTemporal(request,[&](auto r){result=r;done=true;++completions;});
        QTRY_COMPARE(web.calls,3);QVERIFY(client.cancelRecommendation(handle));
        QTRY_VERIFY(done);QCOMPARE(result.status.code,ErrorCode::Cancelled);
        QTest::qWait(600);QCOMPARE(completions,1);
        done=false;client.recommendTemporal(request,[&](auto r){result=r;done=true;++completions;});
        QTRY_COMPARE(web.calls,4);
        bool loggedOut=false;client.logout([&](auto s){status=s;loggedOut=true;});
        QTRY_VERIFY(loggedOut);QVERIFY(status.ok());QTRY_VERIFY(done);
        QTest::qWait(600);QCOMPARE(completions,2);
    }
};
QTEST_GUILESS_MAIN(SceneTests)
#include "scene_recommendation_tests.moc"
