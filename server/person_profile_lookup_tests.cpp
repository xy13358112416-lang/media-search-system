#include "person_profile_lookup.h"
#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QNetworkProxy>
#include <QFile>

class ProfileTests : public QObject {
    Q_OBJECT
private:
    QByteArray reply(QJsonArray people, bool evidence = true) {
        QJsonObject message{{"content", QString::fromUtf8(QJsonDocument(QJsonObject{{"people",people}}).toJson())}};
        if(evidence) message.insert("server_tool_calls", QJsonArray{QJsonObject{{"calls", QJsonArray{QJsonObject{
            {"type","deepseek_web_search"},{"status","completed"},{"kind","search"},
            {"sources",QJsonArray{QJsonObject{{"url","https://example.org/person"}}}}}}}}});
        return QJsonDocument(QJsonObject{{"choices",QJsonArray{QJsonObject{{"finish_reason","stop"},{"message",message}}}}}).toJson();
    }
    QJsonObject person(QString workSource="https://example.org/person") {
        return {{"name",QStringLiteral("周杰伦")},{"source_url","https://example.org/person"},{"biography",QStringLiteral("音乐人")},
            {"works",QJsonArray{QJsonObject{{"title",QStringLiteral("作品A")},{"year","2001"},{"role",QStringLiteral("演唱")},{"source_url",workSource}}}}};
    }
private slots:
    void mobileBaiduNavigationParametersAreNotPageIdentity() {
        // Reduced from the user's 2026-11-24 response; no API key or factual biography retained.
        const QString clean = QString::fromUtf8("https://wapbaike.baidu.com/item/周杰伦/129156");
        auto document = QJsonDocument::fromJson(reply({person("https://not-in-search.example/work")})).object();
        auto choices = document["choices"].toArray();
        auto choice = choices[0].toObject();
        auto message = choice["message"].toObject();
        auto profile = person("https://not-in-search.example/work");
        profile["source_url"] = clean;
        message["content"] = QString::fromUtf8(QJsonDocument(QJsonObject{{"people",QJsonArray{profile}}}).toJson());
        message["server_tool_calls"] = QJsonArray{QJsonObject{{"calls",QJsonArray{QJsonObject{
            {"type","deepseek_web_search"},{"kind","search"},{"status","completed"},
            {"sources",QJsonArray{QJsonObject{{"url",clean + "?fromModule=lemma_starMap&lemmaFrom=lemma_relation_starMap&structureId=tracking&structureClickId=129156&structureItemId=tracking&lemmaIdFrom=3717628#1"}}}}}}}}};
        choice["message"] = message; choices[0] = choice; document["choices"] = choices;
        const auto result = person_profile_lookup::parseReply(QJsonDocument(document).toJson());
        QVERIFY2(result.status.ok(), qPrintable(result.status.message));
        QCOMPARE(result.value->size(), 1);
        QCOMPARE(result.value->first().url, QUrl(clean));
        QVERIFY(result.value->first().works.isEmpty()); // An invented work URL is still rejected.
    }

    void navigationNormalizationIsRestrictedToBaiduNumericItemPages() {
        for (const auto& source : {QString("https://example.org/person?fromModule=other"),
                 QString("https://wapbaike.baidu.com/item/jay?structureId=other"),
                 QString("https://wapbaike.baidu.com/item/jay/999?fromModule=other"),
                 QString("https://wapbaike.baidu.com/item/jay/129156?id=2&fromModule=other"),
                 QString("https://wapbaike.baidu.com/item/jay/129156?oldid=2&fromModule=other")}) {
            auto document = QJsonDocument::fromJson(reply({person()})).object();
            auto choices = document["choices"].toArray(); auto choice = choices[0].toObject();
            auto message = choice["message"].toObject();
            auto profile = person();
            profile["source_url"] = source.startsWith("https://example.org")
                ? QString("https://example.org/person") : QString("https://wapbaike.baidu.com/item/jay/129156");
            message["content"] = QString::fromUtf8(QJsonDocument(QJsonObject{{"people",QJsonArray{profile}}}).toJson());
            message["server_tool_calls"] = QJsonArray{QJsonObject{{"calls",QJsonArray{QJsonObject{
                {"type","deepseek_web_search"},{"kind","search"},{"status","completed"},
                {"sources",QJsonArray{QJsonObject{{"url",source}}}}}}}}};
            choice["message"] = message; choices[0] = choice; document["choices"] = choices;
            QVERIFY(!person_profile_lookup::parseReply(QJsonDocument(document).toJson()).status.ok());
        }
    }

    void malformedFieldsDoNotPretendToBeMissingSources() {
        auto profile = person();
        profile["name"] = "";
        auto result = person_profile_lookup::parseReply(reply({profile}));
        QVERIFY(!result.status.ok());
        QVERIFY(result.status.message.contains(QStringLiteral("姓名或简介")));
        profile = person(); profile["biography"] = QString(1201, 'a');
        result = person_profile_lookup::parseReply(reply({profile}));
        QVERIFY(!result.status.ok());
        QVERIFY(result.status.message.contains(QStringLiteral("姓名或简介")));
    }
    void trackingParametersReferToSamePage() {
        auto body = reply({person()});
        // Public real-response failure reduced to a deterministic fixture.
        body.replace("https://example.org/person", "https://baike.baidu.com/item/jay/129156");
        auto document=QJsonDocument::fromJson(body).object();
        auto choices=document["choices"].toArray();auto choice=choices[0].toObject();auto message=choice["message"].toObject();
        message["server_tool_calls"]=QJsonArray{QJsonObject{{"calls",QJsonArray{QJsonObject{
            {"type","deepseek_web_search"},{"kind","search"},{"status","completed"},
            {"sources",QJsonArray{QJsonObject{{"url","https://baike.baidu.com:443/item/jay/129156?fr=aladdin&bst=tracking#1"}}}}}}}}};
        choice["message"]=message;choices[0]=choice;document["choices"]=choices;
        auto result=person_profile_lookup::parseReply(QJsonDocument(document).toJson());
        QVERIFY(result.status.ok());QCOMPARE(result.value->size(),1);QCOMPARE(result.value->first().works.size(),1);
    }
    void semanticQueryParameterMustNotBeDiscarded() {
        auto body=reply({person()});
        // The model cites /person but evidence is /person?id=2: different resource.
        auto document=QJsonDocument::fromJson(body).object();auto choices=document["choices"].toArray();
        auto choice=choices[0].toObject();auto message=choice["message"].toObject();
        message["server_tool_calls"]=QJsonArray{QJsonObject{{"calls",QJsonArray{QJsonObject{
            {"type","deepseek_web_search"},{"kind","search"},{"status","completed"},
            {"sources",QJsonArray{QJsonObject{{"url","https://example.org/person?id=2"}}}}}}}}};
        choice["message"]=message;choices[0]=choice;document["choices"]=choices;
        QVERIFY(!person_profile_lookup::parseReply(QJsonDocument(document).toJson()).status.ok());
    }
    void optionalRealResponseReplay() {
        const auto path=qEnvironmentVariable("MEDIA_PROFILE_REPLAY");
        if(path.isEmpty()) QSKIP("Set a local redacted response path to replay; no API call.");
        QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));
        auto result=person_profile_lookup::parseReply(file.readAll());
        QVERIFY2(result.status.ok(),qPrintable(result.status.message));QVERIFY(result.value && !result.value->isEmpty());
        // A sourced profile may legitimately have no works: unverified work URLs stay filtered.
        qInfo()<<"Replay accepted"<<result.value->first().title<<"works="<<result.value->first().works.size();
    }
    void fakeHttpReturnsStructuredProfile() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost,0));
        const auto response=reply({person()});
        connect(&server,&QTcpServer::newConnection,&server,[&] {
            auto* socket=server.nextPendingConnection();
            connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
            auto data=std::make_shared<QByteArray>();
            connect(socket,&QTcpSocket::readyRead,socket,[socket,data,response] {
                *data+=socket->readAll();
                if(!data->contains("\r\n\r\n")) return;
                const auto headers=data->left(data->indexOf("\r\n\r\n"));
                int size=0;
                for(const auto& line:headers.split('\n'))
                    if(line.toLower().startsWith("content-length:")) size=line.mid(15).trimmed().toInt();
                if(data->size()<headers.size()+4+size) return;
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: "+QByteArray::number(response.size())+"\r\nConnection: close\r\n\r\n"+response);
                socket->disconnectFromHost();
            });
        });
        QNetworkAccessManager manager; manager.setProxy(QNetworkProxy::NoProxy);
        llm_recommendation::Config config;
        config.apiKey="fake-test-only"; config.endpoint=QUrl(QString("http://127.0.0.1:%1/test").arg(server.serverPort()));
        bool complete=false;
        person_profile_lookup::lookup(manager,QStringLiteral("周杰伦"),config,[&](auto result) {
            complete=true; QVERIFY(result.status.ok()); QCOMPARE(result.value->first().works.size(),1);
        });
        QTRY_VERIFY(complete);
    }
    void timeoutIsAnErrorNotAnEmptySuccess() {
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost,0));
        QNetworkAccessManager manager; manager.setProxy(QNetworkProxy::NoProxy);
        llm_recommendation::Config config;
        config.apiKey="fake-test-only"; config.timeoutMs=100;
        config.endpoint=QUrl(QString("http://127.0.0.1:%1/test").arg(server.serverPort()));
        bool complete=false;
        person_profile_lookup::lookup(manager,"test",config,[&](auto result) {complete=true; QVERIFY(!result.status.ok());});
        QTRY_VERIFY(complete);
    }
    void noKeyDoesNotMakeNetworkRequest() {
        QNetworkAccessManager manager;
        llm_recommendation::Config config;
        config.apiKey.clear();
        bool completed=false;
        person_profile_lookup::lookup(manager,QStringLiteral("周杰伦"),config,[&](auto result) {
            completed=true; QVERIFY(!result.status.ok());
        });
        QVERIFY(completed);
    }
    void structuredWorks() {
        auto result=person_profile_lookup::parseReply(reply({person()}));
        QVERIFY(result.status.ok()); QVERIFY(result.value); QCOMPARE(result.value->size(),1);
        QCOMPARE(result.value->first().works.size(),1);
        QCOMPARE(result.value->first().title,QStringLiteral("周杰伦"));
    }
    void missingSearchEvidenceRejected() { QVERIFY(!person_profile_lookup::parseReply(reply({person()},false)).status.ok()); }
    void fabricatedSourceDropped() {
        auto result=person_profile_lookup::parseReply(reply({person("https://invented.example/work")}));
        QVERIFY(result.status.ok()); QVERIFY(result.value->first().works.isEmpty());
    }
    void successfulEmpty() {
        auto result=person_profile_lookup::parseReply(reply({})); QVERIFY(result.status.ok()); QVERIFY(result.value->isEmpty());
    }
    void truncatedResponseRejected() { QVERIFY(!person_profile_lookup::parseReply("{}").status.ok()); }
};
QTEST_GUILESS_MAIN(ProfileTests)
#include "person_profile_lookup_tests.moc"
