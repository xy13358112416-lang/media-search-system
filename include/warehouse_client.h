#pragma once
#include "warehouse_contract.h"
#include "warehouse_protocol.h"
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QTcpSocket>

// 异步网络客户端；所有方法及回调均在对象所属线程调用。
class WarehouseClient : public QObject, public warehouse_contract::WarehouseService {
public:
    explicit WarehouseClient(QObject* parent = nullptr, int recommendationTimeoutMs = 30000);
    ~WarehouseClient() override;
    void connectToServer(const QString& host, quint16 port, warehouse_contract::StatusCallback done) override;
    void disconnectFromServer() override;
    void login(const QString& username, const QString& password, warehouse_contract::Callback<warehouse_contract::UserInfo> done) override;
    void logout(warehouse_contract::StatusCallback done) override;
    void listMyWarehouses(warehouse_contract::Callback<QList<warehouse_contract::WarehouseSummary>> done) override;
    void listPublicWarehouses(warehouse_contract::Callback<QList<warehouse_contract::WarehouseSummary>> done) override;
    void getWarehouse(qint64 warehouseId, warehouse_contract::Callback<warehouse_contract::WarehouseDetail> done) override;
    void createWarehouse(const QString& name, warehouse_contract::Visibility visibility, warehouse_contract::Callback<warehouse_contract::WarehouseSummary> done) override;
    void addItem(qint64 warehouseId, const MediaRecord& media, warehouse_contract::Callback<warehouse_contract::WarehouseItem> done) override;
    void removeItem(qint64 warehouseId, qint64 itemId, warehouse_contract::StatusCallback done) override;
    warehouse_contract::RecommendationHandle recommendResources(const warehouse_contract::WarehouseRecommendationRequest& request,
                            warehouse_contract::Callback<warehouse_contract::WarehouseRecommendationResult> done) override;
    bool cancelRecommendation(const warehouse_contract::RecommendationHandle& handle) override;
private:
    using Response = std::function<void(warehouse_contract::Status, QJsonObject)>;
    struct Pending { QString responseType; Response callback; bool mutation = false; };
    static warehouse_contract::Status pendingFailure(const Pending& pending, warehouse_contract::Status status);
    QString request(const QString& type, const QJsonObject& payload, Response callback, const QString& logoutToken = {});
    void receive();
    void failAll(warehouse_contract::Status status);
    void abortProtocol();
    void sendCancellation(const QString& targetId);
    void cancelSessionRequests();
    void finishConnect(warehouse_contract::Status status);
    void sendLogin(const QString& username, const QString& password,
                   warehouse_contract::Callback<warehouse_contract::UserInfo> done);
    QTcpSocket socket_;
    warehouse_protocol::FrameDecoder decoder_;
    QHash<QString, Pending> pending_;
    warehouse_contract::StatusCallback connecting_;
    QString token_;
    QString handlePrefix_;
    quint64 nextRequest_ = 0;
    quint64 connectAttempt_ = 0;
    bool loginPending_ = false;
    bool logoutPending_ = false;
    int recommendationTimeoutMs_ = 30000;
};
