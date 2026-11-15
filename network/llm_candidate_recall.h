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
