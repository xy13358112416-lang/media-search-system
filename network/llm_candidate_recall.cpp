// >>> DEFENSE-NOTE | 文件导读｜DeepSeek前的词面候选召回
// 阅读路线：统一文本 → 提取词 → 标题/人物/类别匹配 → 相关优先 → 多类别补充
// 联调关系：只消费已授权候选；控制模型上下文与费用，不负责登录权限，也不等同于模型语义理解。
// 标有“答辩导读”的内容仅为讲解注释；原有条件、接口和算法不变。
// <<< DEFENSE-NOTE | 本段讲解结束
#include "llm_candidate_recall.h"
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace llm_recommendation {
namespace {
// >>> DEFENSE-NOTE | normalized = 文本规范化
// NFKC、大小写折叠，把非字母数字分隔符替换为空格并合并空白，使查询和字段可比较。
// <<< DEFENSE-NOTE | 本段讲解结束
QString normalized(QString value) {
    static const QRegularExpression separators(QStringLiteral("[^\\p{L}\\p{N}]+"));
    value = value.normalized(QString::NormalizationForm_KC).toCaseFolded();
    value.replace(separators, " ");
    return value.simplified();
}
// >>> DEFENSE-NOTE | queryTerms = 提取查询词
// 按空格切词，去常见停用词、短词和重复词；中文无分词器，不能说使用了复杂中文语义分词。
// <<< DEFENSE-NOTE | 本段讲解结束
QStringList queryTerms(const QString& query) {
    static const QSet<QString> stopWords{"a", "an", "the", "of", "to", "in", "on", "for", "and", "with", "by",
        "me", "please", "find", "show", "recommend", "movie", "movies", "film", "films", "some",
        QStringLiteral("推荐"), QStringLiteral("作品"), QStringLiteral("导演"), QStringLiteral("演员")};
    QStringList result;
    for (const auto& term : query.split(' ', Qt::SkipEmptyParts))
        if (term.size() >= 2 && !stopWords.contains(term) && !result.contains(term)) result.append(term);
    return result;
}
// >>> DEFENSE-NOTE | han = 是否汉字
// 帮助区分中英文边界规则，英文不能把单词中间子串随意当完整词。
// <<< DEFENSE-NOTE | 本段讲解结束
bool han(QChar character) { return character.script() == QChar::Script_Han; }
// >>> DEFENSE-NOTE | phraseIn = 带边界的短语包含判断
// 遍历候选出现位置；英文检查左右是否词边界，汉字按本段规则允许短语包含。
// <<< DEFENSE-NOTE | 本段讲解结束
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
// >>> DEFENSE-NOTE | fieldScore = 给单个字段计算词面分
// 完整相等、短语匹配、词项匹配按不同优先级赋分；不匹配返回零。
// <<< DEFENSE-NOTE | 本段讲解结束
int fieldScore(const QString& field, const QString& query, const QStringList& terms, int exact, int phrase, int token) {
    if (query.isEmpty() || field.isEmpty()) return 0;
    if (field == query) return exact;
    if (field.size() >= 2 && (phraseIn(query, field) || phraseIn(field, query))) return phrase;
    int matched = 0;
    for (const auto& term : terms) if (phraseIn(field, term)) ++matched;
    return matched ? token + std::min(matched, 20) : 0;
}
// >>> DEFENSE-NOTE | score = 单个候选的相关性评分
// 去常见媒体后缀，标题高优先，导演/演员其次，类别再次；取各字段最高信号而非简单把所有信号相加。
// <<< DEFENSE-NOTE | 本段讲解结束
int score(const Candidate& candidate, const QString& query, const QStringList& terms) {
    auto title = candidate.media.fileName.trimmed().left(200);
    static const QRegularExpression mediaExtension(QStringLiteral("\\.(mp3|mp4|mkv|wav|flac|avi|mov|m4a|ogg|webm)$"), QRegularExpression::CaseInsensitiveOption);
    title.remove(mediaExtension);
    int value = fieldScore(normalized(title), query, terms, 500, 450, 220);
    value = std::max(value, fieldScore(normalized(candidate.media.director.trimmed().left(80)), query, terms, 350, 350, 150));
    for (const auto& actor : candidate.media.actors)
        value = std::max(value, fieldScore(normalized(actor.trimmed().left(80)), query, terms, 350, 350, 150));
    return std::max(value, fieldScore(normalized(candidate.media.category.trimmed().left(80)), query, terms, 100, 100, 80));
}
struct Ranked { Candidate candidate; QString category; int relevance = 0; };
}

// >>> DEFENSE-NOTE | selectCandidates = 在预算内选模型候选
// 预算限制到0..30；跳过空key/空标题并按key去重。
// 先相关性降序，再浏览次数降序，最后key升序；低热但明确相关的作品不会被无关热门挤掉。
// <<< DEFENSE-NOTE | 本段讲解结束
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
    // >>> DEFENSE-NOTE | 召回步骤｜相关优先后按类别轮流补位
    // 第一轮放入正相关候选；剩余槽位用类别轮转补齐，chosen防重复、categoriesThisRound防一类占满本轮。
    // 补位不表示每一条都已证明语义相关，模型仍可返回空推荐。
    // <<< DEFENSE-NOTE | 本段讲解结束
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

// >>> DEFENSE-NOTE | selectActorMetadata = 选择发送给模型的演员名字
// 演员规范化去重，相关者靠前，最多5个；保留原展示文本。
// stable_sort使同分演员保留原序，不把整部作品所有元数据无限发送。
// <<< DEFENSE-NOTE | 本段讲解结束
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
