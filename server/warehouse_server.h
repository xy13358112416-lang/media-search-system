#pragma once

#include "warehouse_contract.h"
#include "warehouse_protocol.h"
#include <QSqlDatabase>
#include <QTcpServer>

class QTcpSocket;

class WarehouseServer final : public QTcpServer {
public:
    WarehouseServer();
    ~WarehouseServer() override;
    bool openDatabase(const QString& path, QString& error);
    bool addUser(const QString& username, const QString& password, QString& error);
    std::optional<QList<warehouse_contract::WarehouseDetail>> readableCandidates(qint64 userId);

private:
    struct Session {
        warehouse_protocol::FrameDecoder decoder;
        QString token;
        qint64 userId = 0;
    };
    QJsonObject dispatch(Session& session, const QJsonObject& request);
    void readRequests(QTcpSocket* socket, Session& session);
    QString dbName_;
    QSqlDatabase db_;
};
