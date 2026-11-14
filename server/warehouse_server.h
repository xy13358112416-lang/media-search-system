#pragma once
#include <QHostAddress>
#include <QObject>
#include <QSqlDatabase>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSet>
#include <QString>
#include <memory>

// 独立于本地账户库的仓库服务；对象及所有连接在同一个 Qt 线程内运行。
class WarehouseServer : public QObject {
public:
    explicit WarehouseServer(QObject* parent = nullptr);
    ~WarehouseServer() override;
    bool open(const QString& path, QString& error);
    bool addUser(const QString& name, const QString& password, QString& error);
    bool addInitialUser(const QString& name, const QString& password, QString& error);
    bool hasUsers(bool& exists, QString& error) const;
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
