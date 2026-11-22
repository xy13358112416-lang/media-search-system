// >>> MAIN-DEFENSE | 主逻辑头文件导读
// 当前工作区新增的人物资料查询：异步外部资料查找和候选转换；沿用当前代码的身份、来源及生命周期校验，不替代仓库授权。
// 阅读顺序：输入数据结构 → 返回状态/可选数据 → 方法声明 → 私有成员。引用/指针保存依赖，必须确认由谁拥有及何时释放。
// <<< MAIN-DEFENSE
#pragma once
#include "care_contract.h"
#include "llm_recommendation.h"

namespace person_profile_lookup {
// Exposed as a pure parser for offline verification: every returned source must
// occur in completed web-search evidence, not merely in the model's prose.
// >>> MAIN-DEFENSE | 方法声明｜parseReply
// 本行约定调用参数和返回类型；具体分块处理在对应cpp，调用者须遵守已有注释中的权限与生命周期约定。
// <<< MAIN-DEFENSE
care_contract::Result<QList<care_contract::PersonWebSource>> parseReply(const QByteArray& bytes);
void lookup(QNetworkAccessManager& network, const QString& name,
            const llm_recommendation::Config& config,
            care_contract::Callback<QList<care_contract::PersonWebSource>> done);
}
