// >>> MAIN-DEFENSE | 主逻辑头文件导读
// 人物/资源/资讯及分页JSON转换：64位编号和时间使用十进制字符串；严格检查类型、范围和分页字段，防止协议错位。
// 阅读顺序：输入数据结构 → 返回状态/可选数据 → 方法声明 → 私有成员。引用/指针保存依赖，必须确认由谁拥有及何时释放。
// <<< MAIN-DEFENSE
#pragma once

#include "care_contract.h"

#include <QJsonObject>
#include <QJsonValue>

namespace person_json {

// Requests use offset and limit at the payload level. Successful lists carry a page object.
// >>> MAIN-DEFENSE | 方法声明｜encodePageRequest
// 本行约定调用参数和返回类型；具体分块处理在对应cpp，调用者须遵守已有注释中的权限与生命周期约定。
// <<< MAIN-DEFENSE
QJsonObject encodePageRequest(const care_contract::PageRequest& page);
// >>> MAIN-DEFENSE | 方法声明｜decodePageRequest
// 解析平铺offset和limit，offset为十进制字符串，不能混用不一致请求形状。
// <<< MAIN-DEFENSE
bool decodePageRequest(const QJsonObject& payload, care_contract::PageRequest& page);
// >>> MAIN-DEFENSE | 方法声明｜decodeTimeRange
// 解析时间区间，开始结束按共享契约验证，区间使用左闭右开。
// <<< MAIN-DEFENSE
bool decodeTimeRange(const QJsonValue& value, care_contract::TimeRange& range);

// >>> MAIN-DEFENSE | 方法声明｜encode
// 将已校验业务对象编码为JSON，保留稳定标识和缺失字段语义。
// <<< MAIN-DEFENSE
QJsonObject encode(const care_contract::Page<care_contract::PersonSummary>& page);
// >>> MAIN-DEFENSE | 方法声明｜encode
// 将已校验业务对象编码为JSON，保留稳定标识和缺失字段语义。
// <<< MAIN-DEFENSE
QJsonObject encode(const care_contract::Page<care_contract::ResourceSummary>& page);
// >>> MAIN-DEFENSE | 方法声明｜encode
// 将已校验业务对象编码为JSON，保留稳定标识和缺失字段语义。
// <<< MAIN-DEFENSE
QJsonObject encode(const care_contract::Page<care_contract::PersonUpdate>& page);

// >>> MAIN-DEFENSE | 方法声明｜decode
// 将JSON解码并校验字段类型及来源，失败返回空或错误，不擅自补造身份。
// <<< MAIN-DEFENSE
bool decode(const QJsonValue& work, care_contract::ResourceSummary& result);
bool decode(const QJsonValue& page, const care_contract::PageRequest& request,
            care_contract::Page<care_contract::PersonSummary>& result);
bool decode(const QJsonValue& page, const care_contract::PageRequest& request,
            care_contract::Page<care_contract::ResourceSummary>& result);
bool decode(const QJsonValue& page, const care_contract::PageRequest& request,
            care_contract::Page<care_contract::PersonUpdate>& result);

} // namespace person_json
