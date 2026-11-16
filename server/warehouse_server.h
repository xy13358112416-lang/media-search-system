#pragma once

#include "warehouse_contract.h"
#include "warehouse_protocol.h"
#include "behavior_contract.h"
#include <QSqlDatabase>
#include <QTcpServer>

class QTcpSocket;

struct StoredBehavior {
    care_contract::BehaviorRecord record;
    std::optional<MediaRecord> mediaSnapshot; // Historical view metadata, not current access permission.
    bool publicEligible = false; // Eligibility at collection; current source visibility must also be checked.
};

class WarehouseServer final : public QTcpServer {
public:
    WarehouseServer();
    ~WarehouseServer() override;
    bool openDatabase(const QString& path, QString& error);
    bool addUser(const QString& username, const QString& password, QString& error);
    std::optional<QList<warehouse_contract::WarehouseDetail>> readableCandidates(qint64 userId);
    // Internal server API: caller supplies the authenticated user, never a network payload ID.
    std::optional<QList<StoredBehavior>> queryBehavior(qint64 userId, care_contract::TimeRange range);

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
