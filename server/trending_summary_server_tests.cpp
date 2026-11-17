#include "online_care_client.h"
#include "warehouse_server.h"
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QScopeGuard>
#include <QSqlQuery>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

using namespace care_contract;
using warehouse_contract::ErrorCode;

namespace {
bool spin(const std::function<bool()>& ready, int timeout=4000) {
    QElapsedTimer timer;timer.start();
    while(!ready() && timer.elapsed()<timeout) QTest::qWait(5);
    return ready();
}
template<class T,class Start> Result<T> waitResult(Start start) {
    struct State {Result<T> result;bool done=false;};
    auto state=std::make_shared<State>();
    start([state](Result<T> r){state->result=std::move(r);state->done=true;});
    if(!spin([&]{return state->done;})) return {{ErrorCode::Timeout,"Test callback timeout"},{}};
    return state->result;
}
template<class Start> Status waitStatus(Start start) {
    struct State {Status status;bool done=false;};
    auto state=std::make_shared<State>();
    start([state](Status s){state->status=s;state->done=true;});
    if(!spin([&]{return state->done;})) return {ErrorCode::Timeout,"Test callback timeout"};
    return state->status;
}
QJsonObject rawRequest(QTcpSocket& socket,const QJsonObject& request) {
    QString error;const auto bytes=warehouse_protocol::encodeFrame(request,error);
    if(bytes.isEmpty() || socket.write(bytes)<0) return {};
    warehouse_protocol::FrameDecoder decoder;QList<QJsonObject> replies;
    if(!spin([&]{return decoder.feed(socket.readAll(),replies,error) && !replies.isEmpty();})) return {};
    return replies.first();
}
class FakeSummaryHttp : public QTcpServer {
public:
    QList<QByteArray> bodies;
    QList<QPointer<QTcpSocket>> waiting;
    bool hold=false;
    int httpStatus=200;
    FakeSummaryHttp() {
        connect(this,&QTcpServer::newConnection,this,[this]{
            auto* socket=nextPendingConnection();
            auto bytes=std::make_shared<QByteArray>();
            connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
            connect(socket,&QTcpSocket::readyRead,this,[this,socket,bytes]{
                bytes->append(socket->readAll());
                if(socket->property("handled").toBool()) return;
                const auto headerEnd=bytes->indexOf("\r\n\r\n");
                if(headerEnd<0) return;
                qint64 length=-1;
                for(const auto& line:bytes->left(headerEnd).split('\n'))
                    if(line.toLower().startsWith("content-length:")) length=line.mid(15).trimmed().toLongLong();
                if(length<0 || bytes->size()<headerEnd+4+length) return;
                socket->setProperty("handled",true);
                bodies.append(bytes->mid(headerEnd+4,length));waiting.append(socket);
                if(!hold) releaseLast();
            });
        });
    }
    void releaseLast() {
        releaseAt(waiting.size()-1);
    }
    void releaseAt(int index) {
        if(index<0 || index>=waiting.size() || !waiting[index] || waiting[index]->state()!=QAbstractSocket::ConnectedState) return;
        const auto content=QJsonDocument(QJsonObject{{"headline",QStringLiteral("音乐受到关注")},
            {"summary",QStringLiteral("公开榜单反映当前音乐兴趣。")},{"row_ids",QJsonArray{"row-1"}}}).toJson(QJsonDocument::Compact);
        const auto body=QJsonDocument(QJsonObject{{"choices",QJsonArray{QJsonObject{{"finish_reason","stop"},
            {"message",QJsonObject{{"content",QString::fromUtf8(content)}}}}}}}).toJson(QJsonDocument::Compact);
        auto* socket=waiting[index].data();
        socket->write("HTTP/1.1 "+QByteArray::number(httpStatus)+" OK\r\nContent-Type: application/json\r\nContent-Length: "+
                      QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);
        socket->disconnectFromHost();
    }
    llm_recommendation::Config config() const {
        llm_recommendation::Config c;c.apiKey="fake-summary-key";c.timeoutMs=1000;
        c.endpoint=QUrl(QString("http://127.0.0.1:%1/").arg(serverPort()));return c;
    }
};
struct Fixture {
    QTemporaryDir dir;
    FakeSummaryHttp http;
    std::unique_ptr<WarehouseServer> server;
    OnlineCareClient alice,bob,carol;
    TrendingRequest request;
    bool start(bool noKey=false,int modelTimeout=1000) {
        if(!dir.isValid() || !http.listen(QHostAddress::LocalHost)) return false;
        auto config=http.config();config.timeoutMs=modelTimeout;if(noKey) config.apiKey.clear();
        server=std::make_unique<WarehouseServer>(config);QString error;
        if(!server->openDatabase(dir.filePath("summary.sqlite"),error)) return false;
        const QStringList names{"alice","bob","carol"};
        for(const auto& name:names) if(!server->addUser(name,"password-123",error)) return false;
        if(!server->listen(QHostAddress::LocalHost)) return false;
        const auto now=QDateTime::currentSecsSinceEpoch();
        request={TrendingKind::SearchTerms,{now-100,now+100},20};
        int index=0;
        for(auto* client:{&alice,&bob,&carol}) {
            auto connected=waitStatus([&](auto done){client->connectToServer("127.0.0.1",server->serverPort(),done);});
            if(!connected.ok()) return false;
            const auto login=waitResult<warehouse_contract::UserInfo>([&](auto done){client->login(names[index++],"password-123",done);});
            if(!login.status.ok()) return false;
            if(!waitStatus([&](auto done){client->setParticipationSettings({true,true,true},done);}).ok()) return false;
            if(!waitStatus([&](auto done){client->recordBehavior({"search:music",BehaviorAction::Search,{},QStringLiteral("公开音乐")},done);}).ok()) return false;
        }
        return true;
    }
    Result<TrendingResult> rules() {return waitResult<TrendingResult>([&](auto done){alice.getTrending(request,done);});}
    std::optional<ResourceSummary> publicWork() {
        const auto warehouse=waitResult<warehouse_contract::WarehouseSummary>([&](auto cb){
            alice.createWarehouse("Public music",warehouse_contract::Visibility::Public,cb);
        });
        if(!warehouse.value) return {};
        MediaRecord media;media.id="local-only-id";media.fileName="Public music.mp4";
        media.fileType="MP4";media.releaseDate=QDate(2024,1,1);media.creator="private-metadata-sentinel";
        const auto item=waitResult<warehouse_contract::WarehouseItem>([&](auto cb){alice.addItem(warehouse.value->warehouseId,media,cb);});
        if(!item.value) return {};
        const auto work=waitResult<ResourceSummary>([&](auto cb){alice.getResource({warehouse.value->warehouseId,item.value->itemId},cb);});
        if(!work.value) return {};
        for(auto* client:{&alice,&bob,&carol})
            if(!waitStatus([&](auto cb){client->recordBehavior({"view:music",BehaviorAction::View,work.value->resource,{}},cb);}).ok()) return {};
        return work.value;
    }
};
}

class TrendingSummaryServerTests : public QObject {
    Q_OBJECT
private slots:
    void legacyAndInvalidWireModes_data() {
        QTest::addColumn<QJsonValue>("mode");QTest::addColumn<bool>("valid");
        QTest::newRow("missing")<<QJsonValue(QJsonValue::Undefined)<<true;
        QTest::newRow("rules")<<QJsonValue("rules")<<true;
        QTest::newRow("unknown")<<QJsonValue("automatic")<<false;
        QTest::newRow("null")<<QJsonValue(QJsonValue::Null)<<false;
        QTest::newRow("boolean")<<QJsonValue(true)<<false;
    }
    void legacyAndInvalidWireModes() {
        QFETCH(QJsonValue,mode);QFETCH(bool,valid);Fixture f;QVERIFY(f.start());
        QTcpSocket socket;socket.connectToHost(QHostAddress::LocalHost,f.server->serverPort());
        QVERIFY(spin([&]{return socket.state()==QAbstractSocket::ConnectedState;}));
        const auto login=rawRequest(socket,{{"type","login"},{"req_id","login"},
            {"payload",QJsonObject{{"username","alice"},{"password","password-123"}}}});
        const auto token=login.value("payload").toObject().value("token");QVERIFY(token.isString());
        QJsonObject payload{{"kind","search_terms"},{"limit",20},
            {"range",QJsonObject{{"begin_utc",QString::number(f.request.range.beginUtc)},
                                  {"end_utc",QString::number(f.request.range.endUtc)}}}};
        if(!mode.isUndefined()) payload.insert("summary_mode",mode);
        const auto response=rawRequest(socket,{{"type","get_trending"},{"req_id","trend"},{"token",token},{"payload",payload}});
        QCOMPARE(response.value("type").toString(),valid?QString("get_trending_resp"):QString("error_resp"));
        if(valid) QVERIFY(!response.value("payload").toObject().value("result").toObject().value("summary_from_model").toBool());
        else QCOMPARE(response.value("code").toString(),QString("InvalidArgument"));
        QCOMPARE(f.http.bodies.size(),0);
    }
    void rulesNeverCallModelAndExplicitModelPreservesPublicRows() {
        Fixture f;QVERIFY(f.start());
        const auto original=f.rules();QVERIFY(original.status.ok() && original.value);
        QCOMPARE(original.value->items.size(),1);QCOMPARE(f.http.bodies.size(),0);
        f.request.summaryMode=TrendingSummaryMode::Model;
        const auto model=waitResult<TrendingResult>([&](auto done){f.alice.getTrending(f.request,done);});
        QVERIFY(model.status.ok() && model.value);QVERIFY(model.value->summaryFromModel);
        QCOMPARE(f.http.bodies.size(),1);QCOMPARE(model.value->items.size(),1);
        QCOMPARE(model.value->items[0].term,original.value->items[0].term);
        QCOMPARE(model.value->items[0].distinctUsers,original.value->items[0].distinctUsers);
        QCOMPARE(model.value->items[0].effectiveEvents,original.value->items[0].effectiveEvents);
        const auto body=f.http.bodies.first();QVERIFY(!body.contains("alice"));QVERIFY(!body.contains("password-123"));
    }
    void noKeyHttpFailureAndTimeoutFallBack_data() {
        QTest::addColumn<bool>("noKey");QTest::addColumn<int>("httpStatus");QTest::addColumn<bool>("hold");
        QTest::newRow("no-key")<<true<<200<<false;
        QTest::newRow("http-error")<<false<<503<<false;
        QTest::newRow("timeout")<<false<<200<<true;
    }
    void noKeyHttpFailureAndTimeoutFallBack() {
        QFETCH(bool,noKey);QFETCH(int,httpStatus);QFETCH(bool,hold);
        Fixture f;QVERIFY(f.start(noKey,100));f.http.httpStatus=httpStatus;f.http.hold=hold;
        const auto original=f.rules();QVERIFY(original.value);
        f.request.summaryMode=TrendingSummaryMode::Model;
        const auto result=waitResult<TrendingResult>([&](auto done){f.alice.getTrending(f.request,done);});
        QVERIFY(result.status.ok() && result.value);QVERIFY(!result.value->summaryFromModel);
        QCOMPARE(result.value->summary,original.value->summary);QCOMPARE(result.value->items.size(),1);
        QCOMPARE(f.http.bodies.size(),noKey?0:1);
    }
    void publicEligibilityChangeDiscardsOldSummary_data() {
        QTest::addColumn<int>("change");QTest::newRow("withdraw")<<0;QTest::newRow("clear-history")<<1;
        QTest::newRow("new-public-term")<<2;
        QTest::newRow("same-term-new-count")<<3;
    }
    void publicEligibilityChangeDiscardsOldSummary() {
        QFETCH(int,change);Fixture f;QVERIFY(f.start());f.http.hold=true;
        f.request.summaryMode=TrendingSummaryMode::Model;
        bool done=false;Result<TrendingResult> result;
        auto cleanup=qScopeGuard([&]{f.alice.disconnectFromServer();});
        f.alice.getTrending(f.request,[&](auto r){result=r;done=true;});
        QVERIFY(spin([&]{return f.http.bodies.size()==1;}));QVERIFY(!done);
        Status changed;
        if(change==0) changed=waitStatus([&](auto cb){f.carol.setParticipationSettings({true,true,false},cb);});
        else if(change==1) changed=waitStatus([&](auto cb){f.carol.clearHistory(cb);});
        else if(change==2) for(auto* client:{&f.alice,&f.bob,&f.carol}) {
            changed=waitStatus([&](auto cb){client->recordBehavior({"search:other",BehaviorAction::Search,{},QStringLiteral("另一公开话题")},cb);});
            QVERIFY(changed.ok());
        }
        else {
            QString error;QVERIFY(f.server->addUser("dave","password-123",error));OnlineCareClient extra;
            QVERIFY(waitStatus([&](auto cb){extra.connectToServer("127.0.0.1",f.server->serverPort(),cb);}).ok());
            QVERIFY(waitResult<warehouse_contract::UserInfo>([&](auto cb){extra.login("dave","password-123",cb);}).status.ok());
            QVERIFY(waitStatus([&](auto cb){extra.setParticipationSettings({true,true,true},cb);}).ok());
            changed=waitStatus([&](auto cb){extra.recordBehavior({"search:music",BehaviorAction::Search,{},QStringLiteral("公开音乐")},cb);});
        }
        QVERIFY(changed.ok());f.http.releaseLast();QVERIFY(spin([&]{return done;}));
        QVERIFY(result.status.ok() && result.value);QVERIFY(!result.value->summaryFromModel);
        QCOMPARE(result.value->items.size(),change==2?2:change==3?1:0);
        if(change==3) QCOMPARE(result.value->items.first().distinctUsers,4);
        QCOMPARE(f.http.bodies.size(),1);
    }
    void publicWorkSourceChangeDiscardsOldSummary_data() {
        QTest::addColumn<bool>("makePrivate");QTest::newRow("remove-source")<<false;QTest::newRow("make-private")<<true;
    }
    void publicWorkSourceChangeDiscardsOldSummary() {
        QFETCH(bool,makePrivate);Fixture f;QVERIFY(f.start());const auto work=f.publicWork();QVERIFY(work);
        f.request.kind=TrendingKind::Works;f.request.summaryMode=TrendingSummaryMode::Model;f.http.hold=true;
        Result<TrendingResult> result;bool done=false;auto cleanup=qScopeGuard([&]{f.alice.disconnectFromServer();});
        f.alice.getTrending(f.request,[&](auto r){result=r;done=true;});
        QVERIFY(spin([&]{return f.http.bodies.size()==1;}));
        QVERIFY(!f.http.bodies.first().contains("private-metadata-sentinel"));
        if(makePrivate) {
            {
                auto db=QSqlDatabase::addDatabase("QSQLITE","summary-visibility-test");
                db.setDatabaseName(f.dir.filePath("summary.sqlite"));QVERIFY(db.open());QSqlQuery q(db);
                q.prepare("UPDATE warehouses SET is_public=0 WHERE id=?");q.addBindValue(work->resource.source.warehouseId);
                QVERIFY(q.exec());QCOMPARE(q.numRowsAffected(),1);db.close();
            }
            QSqlDatabase::removeDatabase("summary-visibility-test");
        } else QVERIFY(waitStatus([&](auto cb){f.alice.removeItem(work->resource.source.warehouseId,work->resource.source.itemId,cb);}).ok());
        f.http.releaseLast();QVERIFY(spin([&]{return done;}));
        QVERIFY(result.status.ok() && result.value);QVERIFY(!result.value->summaryFromModel);QVERIFY(result.value->items.isEmpty());
        QCOMPARE(f.http.bodies.size(),1);
    }
    void serverStaysResponsiveDuringModelWait() {
        Fixture f;QVERIFY(f.start());f.http.hold=true;f.request.summaryMode=TrendingSummaryMode::Model;
        bool done=false;auto cleanup=qScopeGuard([&]{f.alice.disconnectFromServer();});
        f.alice.getTrending(f.request,[&](auto){done=true;});
        QVERIFY(spin([&]{return f.http.bodies.size()==1;}));QVERIFY(!done);
        const auto settings=waitResult<ParticipationSettings>([&](auto cb){f.alice.getParticipationSettings(cb);});
        QVERIFY(settings.status.ok());QVERIFY(!done);f.http.releaseLast();QVERIFY(spin([&]{return done;}));
    }
    void onePendingSummaryPerSessionAndCapacityRecovers()
    {
        Fixture f;
        QVERIFY(f.start(false, 3500));
        f.http.hold = true;
        auto model = f.request;
        model.summaryMode = TrendingSummaryMode::Model;
        int aliceCalls = 0, bobCalls = 0;
        Result<TrendingResult> aliceResult, bobResult;
        auto cleanup = qScopeGuard([&] {
            f.alice.disconnectFromServer();
            f.bob.disconnectFromServer();
        });
        f.alice.getTrending(model, [&](auto result) { aliceResult = std::move(result); ++aliceCalls; });
        QVERIFY(spin([&] { return f.http.bodies.size() == 1; }));
        const auto duplicate = waitResult<TrendingResult>([&](auto done) { f.alice.getTrending(model, done); });
        QCOMPARE(duplicate.status.code, ErrorCode::ServerError);
        QVERIFY(duplicate.status.message.contains(QStringLiteral("当前会话")));
        QCOMPARE(aliceCalls, 0);
        QCOMPARE(f.http.bodies.size(), 1);
        QVERIFY(f.rules().status.ok());

        f.bob.getTrending(model, [&](auto result) { bobResult = std::move(result); ++bobCalls; });
        QVERIFY(spin([&] { return f.http.bodies.size() == 2; }));
        f.http.releaseAt(1);
        QVERIFY(spin([&] { return bobCalls == 1; }));
        QVERIFY(bobResult.status.ok() && bobResult.value && bobResult.value->summaryFromModel);
        QCOMPARE(aliceCalls, 0);
        f.http.releaseAt(0);
        QVERIFY(spin([&] { return aliceCalls == 1; }));
        QVERIFY(aliceResult.status.ok() && aliceResult.value && aliceResult.value->summaryFromModel);
        f.http.hold = false;
        const auto after = waitResult<TrendingResult>([&](auto done) { f.alice.getTrending(model, done); });
        QVERIFY(after.status.ok() && after.value && after.value->summaryFromModel);
        QCOMPARE(f.http.bodies.size(), 3);
        QCOMPARE(aliceCalls, 1);
        QCOMPARE(bobCalls, 1);
    }

    void cancellingWarehouseRecommendationDoesNotCancelSummaryWithSameModelId() {
        Fixture f;QVERIFY(f.start());QVERIFY(f.publicWork());f.http.hold=true;
        f.request.summaryMode=TrendingSummaryMode::Model;
        int summaryCalls=0,recommendationCalls=0;Result<TrendingResult> summary;
        warehouse_contract::Result<warehouse_contract::WarehouseRecommendationResult> recommendation;
        auto cleanup=qScopeGuard([&]{f.alice.disconnectFromServer();});
        f.alice.getTrending(f.request,[&](auto r){summary=r;++summaryCalls;});
        QVERIFY(spin([&]{return f.http.bodies.size()==1;}));
        const auto handle=f.alice.recommendResources({"music",{},5},[&](auto r){recommendation=r;++recommendationCalls;});
        QVERIFY(!handle.isEmpty());QVERIFY(spin([&]{return f.http.bodies.size()==2;}));
        QVERIFY(f.alice.cancelRecommendation(handle));QCOMPARE(recommendationCalls,1);
        QCOMPARE(recommendation.status.code,ErrorCode::Cancelled);
        QVERIFY(spin([&]{return !f.http.waiting[1] || f.http.waiting[1]->state()==QAbstractSocket::UnconnectedState;}));
        QVERIFY(f.http.waiting[0] && f.http.waiting[0]->state()==QAbstractSocket::ConnectedState);QCOMPARE(summaryCalls,0);
        f.http.releaseAt(0);QVERIFY(spin([&]{return summaryCalls==1;}));
        QVERIFY(summary.status.ok() && summary.value && summary.value->summaryFromModel);QCOMPARE(recommendationCalls,1);
    }
    void logoutDisconnectAndDestructionCancelWithoutLateCompletion_data() {
        QTest::addColumn<int>("action");QTest::newRow("logout")<<0;QTest::newRow("disconnect")<<1;QTest::newRow("destroy")<<2;
    }
    void logoutDisconnectAndDestructionCancelWithoutLateCompletion() {
        QFETCH(int,action);Fixture f;QVERIFY(f.start());f.http.hold=true;f.request.summaryMode=TrendingSummaryMode::Model;
        int calls=0;Result<TrendingResult> result;
        auto cleanup=qScopeGuard([&]{f.alice.disconnectFromServer();});
        f.alice.getTrending(f.request,[&](auto r){result=r;++calls;});
        QVERIFY(spin([&]{return f.http.bodies.size()==1;}));
        if(action==0) QVERIFY(waitStatus([&](auto cb){f.alice.logout(cb);}).ok());
        else if(action==1) f.alice.disconnectFromServer();
        else f.server.reset();
        QVERIFY(spin([&]{return calls==1;}));QVERIFY(!result.status.ok());
        QVERIFY(spin([&]{return !f.http.waiting.last() || f.http.waiting.last()->state()==QAbstractSocket::UnconnectedState;}));
        f.http.releaseLast();QTest::qWait(60);QCOMPARE(calls,1);
        if(action==0) {
            const auto login=waitResult<warehouse_contract::UserInfo>([&](auto cb){f.alice.login("bob","password-123",cb);});
            QVERIFY(login.status.ok());const auto own=waitResult<ParticipationSettings>([&](auto cb){f.alice.getParticipationSettings(cb);});
            QVERIFY(own.status.ok());QCOMPARE(calls,1);
        }
    }
};
QTEST_GUILESS_MAIN(TrendingSummaryServerTests)
#include "trending_summary_server_tests.moc"
