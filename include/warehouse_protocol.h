#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace warehouse_protocol {
constexpr quint32 MaxFrameSize = 4U * 1024U * 1024U;
enum class MessageType { Login, Logout, ListMyWarehouses, ListPublicWarehouses,
                         GetWarehouse, CreateWarehouse, AddItem, RemoveItem, RecommendResources,
                         CancelRecommendation, ErrorResponse };
QString messageName(MessageType type);
// 大端 4 字节长度 + UTF-8 JSON 对象；失败时返回空数组并填写 error。
QByteArray encodeFrame(const QJsonObject& object, QString& error);
class FrameDecoder {
public:
    // 追加解析的完整对象；失败后必须关闭连接或 reset，已输出的对象仍有效。
    bool feed(const QByteArray& bytes, QList<QJsonObject>& output, QString& error);
    void reset();
private:
    QByteArray buffer_;
    quint32 expected_ = 0;
    bool failed_ = false;
};
}
