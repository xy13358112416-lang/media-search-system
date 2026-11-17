#include "llm_candidate_recall.h"
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace llm_recommendation {
namespace {
QString normalized(QString value) {
    static const QRegularExpression separators(QStringLiteral("[^\\p{L}\\p{N}]+"));
    value = value.normalized(QString::NormalizationForm_KC).toCaseFolded();
    value.replace(separators, " ");
    return value.simplified();
}
QStringList queryTerms(const QString& query) {
    static const QSet<QString> stopWords{"a", "an", "the", "of", "to", "in", "on", "for", "and", "with", "by",
        "me", "please", "find", "show", "recommend", "movie", "movies", "film", "films", "some",
        QStringLiteral("推荐"), QStringLiteral("作品"), QStringLiteral("导演"), QStringLiteral("演员")};
    QStringList result;
    for (const auto& term : query.split(' ', Qt::SkipEmptyParts))
        if (term.size() >= 2 && !stopWords.contains(term) && !result.contains(term)) result.append(term);
    return result;
}
bool han(QChar character) { return character.script() == QChar::Script_Han; }
bool phraseIn(const QString& text, const QString& phrase) {
    if (phrase.isEmpty()) return false;
    qsizetype offset = 0;
    while ((offset = text.indexOf(phrase, offset)) >= 0) {
        const auto end = offset + phrase.size();
        const bool left = han(phrase.front()) || offset == 0 || !text[offset - 1].isLetterOrNumber();
        const bool right = han(phrase.back()) || end == text.size() || !text[end].isLetterOrNumber();
        if (left && right) return true;
        ++offset;
    }
    return false;
}
int fieldScore(const QString& field, const QString& query, const QStringList& terms, int exact, int phrase, int token) {
    if (query.isEmpty() || field.isEmpty()) return 0;
    if (field == query) return exact;
    if (field.size() >= 2 && (phraseIn(query, field) || phraseIn(field, query))) return phrase;
    int matched = 0;
    for (const auto& term : terms) if (phraseIn(field, term)) ++matched;
    return matched ? token + std::min(matched, 20) : 0;
}
int score(const Candidate& candidate, const QString& query, const QStringList& terms) {
    auto title = candidate.media.fileName.trimmed().left(200);
    static const QRegularExpression mediaExtension(QStringLiteral("\\.(mp3|mp4|mkv|wav|flac|avi|mov|m4a|ogg|webm)$"), QRegularExpression::CaseInsensitiveOption);
    title.remove(mediaExtension);
    int value = fieldScore(normalized(title), query, terms, 500, 450, 220);
    value = std::max(value, fieldScore(normalized(candidate.media.creator.trimmed().left(80)), query, terms, 350, 350, 150));
    for (const auto& actor : candidate.media.actors)
        value = std::max(value, fieldScore(normalized(actor.trimmed().left(80)), query, terms, 350, 350, 150));
    return std::max(value, fieldScore(normalized(candidate.media.category.trimmed().left(80)), query, terms, 100, 100, 80));
}
struct Ranked { Candidate candidate; QString category; int relevance = 0; };
}

QList<Candidate> selectCandidates(const Request& request, int budget) {
    budget = std::clamp(budget, 0, CandidateBudget);
    if (!budget) return {};
    const auto query = normalized(request.context.left(ContextUnits));
    const auto terms = queryTerms(query);
    QList<Ranked> ranked;
    QSet<QString> seen;
    for (const auto& candidate : request.candidates) {
        if (candidate.key.trimmed().isEmpty() || candidate.media.fileName.trimmed().isEmpty() || seen.contains(candidate.key)) continue;
        seen.insert(candidate.key);
        ranked.append({candidate, normalized(candidate.media.category), score(candidate, query, terms)});
    }
    std::sort(ranked.begin(), ranked.end(), [](const Ranked& a, const Ranked& b) {
        if (a.relevance != b.relevance) return a.relevance > b.relevance;
        if (a.candidate.media.viewCount != b.candidate.media.viewCount) return a.candidate.media.viewCount > b.candidate.media.viewCount;
        return a.candidate.key < b.candidate.key;
    });
    QList<Candidate> result;
    QSet<QString> chosen, categoriesThisRound;
    const auto append = [&](const Ranked& item) {
        result.append(item.candidate); chosen.insert(item.candidate.key); categoriesThisRound.insert(item.category);
    };
    // Relevant cold resources must survive even when one unrelated category dominates heat.
    for (const auto& item : ranked) {
        if (result.size() >= budget || item.relevance == 0) break;
        append(item);
    }
    // With leftover space, give each category one turn before letting the same
    // category fill another slot. Within each turn relevance/heat/key order stays stable.
    for (int round = 0; round <= budget && result.size() < budget && chosen.size() < ranked.size(); ++round) {
        for (const auto& item : ranked) {
            if (chosen.contains(item.candidate.key) || categoriesThisRound.contains(item.category)) continue;
            append(item);
            if (result.size() >= budget) break;
        }
        categoriesThisRound.clear();
    }
    return result;
}

QStringList selectActorMetadata(const QStringList& actors, const QString& context) {
    const auto query = normalized(context.left(ContextUnits));
    const auto terms = queryTerms(query);
    QList<QPair<QString, int>> ordered;
    QSet<QString> seen;
    for (const auto& actor : actors) {
        const auto text = actor.trimmed().left(80);
        const auto norm = normalized(text);
        if (norm.isEmpty() || seen.contains(norm)) continue;
        seen.insert(norm); ordered.append({text, fieldScore(norm, query, terms, 350, 350, 150)});
    }
    std::stable_sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    QStringList selected;
    for (const auto& actor : ordered) {
        selected.append(actor.first);
        if (selected.size() == 5) break;
    }
    return selected;
}
} // namespace llm_recommendation
