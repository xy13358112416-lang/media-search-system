// >>> MAIN-DEFENSE | 主逻辑头文件导读
// 共享接口/数据契约：上层通过这里的类型和方法与主逻辑对接。声明不等于执行，实际实现见对应cpp。
// 阅读顺序：输入数据结构 → 返回状态/可选数据 → 方法声明 → 私有成员。引用/指针保存依赖，必须确认由谁拥有及何时释放。
// <<< MAIN-DEFENSE
#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace warehouse_protocol {
constexpr quint32 MaxFrameSize = 4U * 1024U * 1024U;
enum class MessageType { Login, Logout, ListMyWarehouses, ListPublicWarehouses,
                         GetWarehouse, CreateWarehouse, AddItem, RemoveItem, RecommendResources,
                         CancelRecommendation, ErrorResponse,
                         RecordBehavior, GetParticipationSettings, SetParticipationSettings, ClearHistory,
                         ListFollows, SetFollow, GetPersonWorks, GetPersonUpdates,
                         GetTrending, GetFootprint, RecommendTemporal, ListWorkFeedback, SetWorkFeedback,
                         GetResource, SearchPeople };
// >>> MAIN-DEFENSE | 方法声明｜messageName
// 本行约定调用参数和返回类型；具体分块处理在对应cpp，调用者须遵守已有注释中的权限与生命周期约定。
// <<< MAIN-DEFENSE
QString messageName(MessageType type);
// 大端 4 字节长度 + UTF-8 JSON 对象；失败时返回空数组并填写 error。
// >>> MAIN-DEFENSE | 方法声明｜encodeFrame
// 将JSON转UTF-8正文，前置4字节大端长度；限制帧大小，TCP读写不以read调用划分消息。
// <<< MAIN-DEFENSE
QByteArray encodeFrame(const QJsonObject& object, QString& error);
// >>> MAIN-DEFENSE | 接口结构｜FrameDecoder
// 这是服务对象：public为调用入口，private保存内部状态；Qt或其他模块通过接口调用，业务权限在实现中校验。
// <<< MAIN-DEFENSE
class FrameDecoder {
public:
    // 追加解析的完整对象；失败后必须关闭连接或 reset，已输出的对象仍有效。
    // >>> MAIN-DEFENSE | 方法声明｜feed
    // 把新字节追加到解码状态，长度头不完整就等；正文完整才解JSON，一次输入可以产出多帧。
    // <<< MAIN-DEFENSE
    bool feed(const QByteArray& bytes, QList<QJsonObject>& output, QString& error);
    // >>> MAIN-DEFENSE | 方法声明｜reset
    // 清理协议缓冲、预计长度和错误状态，供新连接重新使用。
    // <<< MAIN-DEFENSE
    void reset();
private:
    QByteArray buffer_;
    quint32 expected_ = 0;
    bool failed_ = false;
};
}
