#pragma once

#include "warehouse_contract.h"
#include "warehouse_protocol.h"
#include "llm_recommendation.h"
#include <QHash>
#include <QSet>
#include <QSqlDatabase>
#include <QTcpServer>
#include <memory>

class QTcpSocket;

class WarehouseServer final : public QTcpServer {
public:
    explicit WarehouseServer(llm_recommendation::Config config = llm_recommendation::loadConfig());
    ~WarehouseServer() override;
    bool openDatabase(const QString& path, QString& error);
    bool addUser(const QString& username, const QString& password, QString& error);
    std::optional<QList<warehouse_contract::WarehouseDetail>> readableCandidates(qint64 userId);

private:
    struct Session {
        struct Pending {
            quint64 modelId = 0;
            QString token;
            qint64 userId = 0;
            std::optional<qint64> target;
            QSet<QString> candidateKeys;
        };
        warehouse_protocol::FrameDecoder decoder;
        QString token;
        qint64 userId = 0;
        QHash<QString, Pending> pending;
    };
    QJsonObject dispatch(Session& session, const QJsonObject& request);
    void readRequests(QTcpSocket* socket, const std::shared_ptr<Session>& session);
    bool sendResponse(QTcpSocket* socket, const QJsonObject& response);
    void startRecommendation(QTcpSocket* socket, const std::shared_ptr<Session>& session, const QJsonObject& request);
    void cancelPending(Session& session, QTcpSocket* socket, bool notify);
    llm_recommendation::Client recommender_;
    QString dbName_;
    QSqlDatabase db_;
};
