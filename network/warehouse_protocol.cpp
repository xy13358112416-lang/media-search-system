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
    case MessageType::RecordBehavior: return QStringLiteral("record_behavior");
    case MessageType::GetParticipationSettings: return QStringLiteral("get_participation_settings");
    case MessageType::SetParticipationSettings: return QStringLiteral("set_participation_settings");
    case MessageType::ClearHistory: return QStringLiteral("clear_history");
    case MessageType::ListFollows: return QStringLiteral("list_follows");
    case MessageType::SetFollow: return QStringLiteral("set_follow");
    case MessageType::GetPersonWorks: return QStringLiteral("get_person_works");
    case MessageType::GetPersonUpdates: return QStringLiteral("get_person_updates");
    case MessageType::GetTrending: return QStringLiteral("get_trending");
    case MessageType::GetFootprint: return QStringLiteral("get_footprint");
    case MessageType::RecommendTemporal: return QStringLiteral("recommend_temporal");
    case MessageType::ListWorkFeedback: return QStringLiteral("list_work_feedback");
    case MessageType::SetWorkFeedback: return QStringLiteral("set_work_feedback");
    case MessageType::GetResource: return QStringLiteral("get_resource");
    case MessageType::SearchPeople: return QStringLiteral("search_people");
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
