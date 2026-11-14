#pragma once
#include <QHostAddress>
#include <QObject>
#include <QSqlDatabase>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSet>
#include <QString>
#include <memory>
#include "warehouse_contract.h"

// 独立于本地账户库的仓库服务；对象及所有连接在同一个 Qt 线程内运行。
class WarehouseServer : public QObject {
public:
    explicit WarehouseServer(QObject* parent = nullptr);
    ~WarehouseServer() override;
    bool open(const QString& path, QString& error);
    bool addUser(const QString& name, const QString& password, QString& error);
    bool addInitialUser(const QString& name, const QString& password, QString& error);
    bool hasUsers(bool& exists, QString& error) const;
    // 仅限服务器内部使用：userId 必须来自已认证连接，不能使用客户端提交的 ID。
    // 成功时替换 out，失败时保持原值；结果只包含本人或公开仓库及其媒体快照。
    bool readableWarehouses(qint64 userId, QList<warehouse_contract::WarehouseDetail>& out, QString& error) const;
    bool listen(const QHostAddress& address, quint16 port, QString& error);
    quint16 port() const;
    void close();
private:
    struct Connection;
    void acceptConnections();
    void receive(const std::shared_ptr<Connection>& connection);
    void process(const std::shared_ptr<Connection>& connection, const QJsonObject& request);
    QTcpServer listener_;
    QSqlDatabase db_;
    QString dbName_;
    QSet<QTcpSocket*> sockets_;
};
