// >>> DEFENSE-NOTE | 头文件导读｜词面候选召回接口
// 最多30候选和有限上下文。与DeepSeek配合做输入压缩，不负责用户认证，也不保证词面补位一定语义相关。
// 读法：struct说明传哪些数据；class/函数声明说明谁可调用；实际算法去同名.cpp或引用的实现文件。
// <<< DEFENSE-NOTE | 本段讲解结束
#pragma once
#include "llm_recommendation.h"

namespace llm_recommendation {
constexpr int CandidateBudget = 30;
constexpr int ContextUnits = 2000;
// Pure, deterministic lexical recall over an ALREADY authorized snapshot.
// Explicit title/person/category matches precede diversity/heat fillers. This is
// a bounded candidate pool, not a semantic verdict that every item is relevant.
QList<Candidate> selectCandidates(const Request& request, int budget = CandidateBudget);
// Same lexical signals preserve a matching credited person within the 5-name payload cap.
QStringList selectActorMetadata(const QStringList& actors, const QString& context);
} // namespace llm_recommendation
