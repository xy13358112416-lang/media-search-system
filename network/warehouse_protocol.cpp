// >>> MAIN-DEFENSE | 主逻辑文件导读
// TCP帧协议：4字节大端长度+JSON正文。TCP只提供字节流，feed通过缓冲区处理半包和多帧；一次read不等于一个完整请求。
// 阅读时先看函数入口说明，再按“处理阶段”往下；到每个return核对成功、空结果和错误是否区别。
// <<< MAIN-DEFENSE
#include "warehouse_protocol.h"
#include <QJsonDocument>
#include <QJsonParseError>
#include <QtEndian>
#include <algorithm>

namespace warehouse_protocol {
// >>> MAIN-DEFENSE | 函数入口｜messageName
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：QString messageName(MessageType type) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
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
    // >>> MAIN-DEFENSE | messageName｜处理阶段 01
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return {};
}
// >>> MAIN-DEFENSE | 函数入口｜encodeFrame
// 将JSON转UTF-8正文，前置4字节大端长度；限制帧大小，TCP读写不以read调用划分消息。
// 签名提示：QByteArray encodeFrame(const QJsonObject& object, QString& error) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
QByteArray encodeFrame(const QJsonObject& object, QString& error) {
    error.clear();
    const QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (payload.isEmpty() || payload.size() > MaxFrameSize) { error = QStringLiteral("JSON 帧超过大小限制"); return {}; }
    QByteArray result(4, '\0'); qToBigEndian<quint32>(static_cast<quint32>(payload.size()), reinterpret_cast<uchar*>(result.data()));
    result.append(payload); return result;
}
// >>> MAIN-DEFENSE | 函数入口｜FrameDecoder::feed
// 把新字节追加到解码状态，长度头不完整就等；正文完整才解JSON，一次输入可以产出多帧。
// 签名提示：bool FrameDecoder::feed(const QByteArray& bytes, QList<QJsonObject>& output, QString& error) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool FrameDecoder::feed(const QByteArray& bytes, QList<QJsonObject>& output, QString& error) {
    error.clear();
    if (failed_) { error = QStringLiteral("解码器已失败，请重置"); return false; }
    qsizetype offset = 0;
    // >>> MAIN-DEFENSE | feed｜处理阶段 01
    // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
    // <<< MAIN-DEFENSE
    while (offset < bytes.size()) {
        const qsizetype needed = expected_ == 0 ? 4 : static_cast<qsizetype>(expected_);
        const qsizetype count = std::min(needed - buffer_.size(), bytes.size() - offset);
        buffer_.append(bytes.constData() + offset, count); offset += count;
        // >>> MAIN-DEFENSE | feed｜处理阶段 02
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (buffer_.size() < needed) continue;
        if (expected_ == 0) {
            expected_ = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(buffer_.constData()));
            buffer_.clear();
            // >>> MAIN-DEFENSE | feed｜处理阶段 03
            // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
            // <<< MAIN-DEFENSE
            if (expected_ == 0 || expected_ > MaxFrameSize) {
                failed_ = true; error = QStringLiteral("无效帧长度"); return false;
            }
        } else {
            QJsonParseError parseError;
            const auto document = QJsonDocument::fromJson(buffer_, &parseError);
            buffer_.clear(); expected_ = 0;
            // >>> MAIN-DEFENSE | feed｜处理阶段 04
            // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
            // <<< MAIN-DEFENSE
            if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
                failed_ = true; error = QStringLiteral("帧内容不是有效的 JSON 对象"); return false;
            }
            output.append(document.object());
        }
    }
    // >>> MAIN-DEFENSE | feed｜处理阶段 05
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return true;
}
// >>> MAIN-DEFENSE | 函数入口｜FrameDecoder::reset
// 清理协议缓冲、预计长度和错误状态，供新连接重新使用。
// 签名提示：void FrameDecoder::reset() 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
void FrameDecoder::reset() { buffer_.clear(); expected_ = 0; failed_ = false; }
}
