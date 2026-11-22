// >>> MAIN-DEFENSE | 主逻辑头文件导读
// 共享接口/数据契约：上层通过这里的类型和方法与主逻辑对接。声明不等于执行，实际实现见对应cpp。
// 阅读顺序：输入数据结构 → 返回状态/可选数据 → 方法声明 → 私有成员。引用/指针保存依赖，必须确认由谁拥有及何时释放。
// <<< MAIN-DEFENSE
#pragma once
#include "mediarecord.h"
#include <QJsonObject>

namespace media_json {
// 转换十个字段；两个 quint64 使用十进制字符串保证无损。
// >>> MAIN-DEFENSE | 方法声明｜toJson
// 本行约定调用参数和返回类型；具体分块处理在对应cpp，调用者须遵守已有注释中的权限与生命周期约定。
// <<< MAIN-DEFENSE
QJsonObject toJson(const MediaRecord& record);
// 校验失败时返回 false、填写 error，不修改 record。
// >>> MAIN-DEFENSE | 方法声明｜fromJson
// 本行约定调用参数和返回类型；具体分块处理在对应cpp，调用者须遵守已有注释中的权限与生命周期约定。
// <<< MAIN-DEFENSE
bool fromJson(const QJsonObject& json, MediaRecord& record, QString& error);
}
