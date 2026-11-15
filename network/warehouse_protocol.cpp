#include "warehouse_protocol.h"
#include <QJsonDocument>
#include <QJsonParseError>
#include <QtEndian>
#include <algorithm>

namespace warehouse_protocol {
QString messageName(MessageType type) {
    switch (type) {
    case MessageType::Login: return QStringLiteral("login");
    case MessageType::Logout: return QStringLiteral("logout");
    case MessageType::ListMyWarehouses: return QStringLiteral("list_my_warehouses");
    case MessageType::ListPublicWarehouses: return QStringLiteral("list_public_warehouses");
    case MessageType::GetWarehouse: return QStringLiteral("get_warehouse");
    case MessageType::CreateWarehouse: return QStringLiteral("create_warehouse");
    case MessageType::AddItem: return QStringLiteral("add_item");
    case MessageType::RemoveItem: return QStringLiteral("remove_item");
    case MessageType::RecommendResources: return QStringLiteral("recommend_resources");
    case MessageType::CancelRecommendation: return QStringLiteral("cancel_recommendation");
    case MessageType::ErrorResponse: return QStringLiteral("error_resp");
    }
    return {};
}
QByteArray encodeFrame(const QJsonObject& object, QString& error) {
    error.clear();
    const QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (payload.isEmpty() || payload.size() > MaxFrameSize) { error = QStringLiteral("JSON 帧超过大小限制"); return {}; }
    QByteArray result(4, '\0'); qToBigEndian<quint32>(static_cast<quint32>(payload.size()), reinterpret_cast<uchar*>(result.data()));
    result.append(payload); return result;
}
bool FrameDecoder::feed(const QByteArray& bytes, QList<QJsonObject>& output, QString& error) {
    error.clear();
    if (failed_) { error = QStringLiteral("解码器已失败，请重置"); return false; }
    qsizetype offset = 0;
    while (offset < bytes.size()) {
        const qsizetype needed = expected_ == 0 ? 4 : static_cast<qsizetype>(expected_);
        const qsizetype count = std::min(needed - buffer_.size(), bytes.size() - offset);
        buffer_.append(bytes.constData() + offset, count); offset += count;
        if (buffer_.size() < needed) continue;
        if (expected_ == 0) {
            expected_ = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(buffer_.constData()));
            buffer_.clear();
            if (expected_ == 0 || expected_ > MaxFrameSize) {
                failed_ = true; error = QStringLiteral("无效帧长度"); return false;
            }
        } else {
            QJsonParseError parseError;
            const auto document = QJsonDocument::fromJson(buffer_, &parseError);
            buffer_.clear(); expected_ = 0;
            if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
                failed_ = true; error = QStringLiteral("帧内容不是有效的 JSON 对象"); return false;
            }
            output.append(document.object());
        }
    }
    return true;
}
void FrameDecoder::reset() { buffer_.clear(); expected_ = 0; failed_ = false; }
}
