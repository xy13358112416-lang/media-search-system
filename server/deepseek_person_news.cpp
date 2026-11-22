// >>> DEFENSE-NOTE | 文件导读｜人物资讯Search与Fetch
// 阅读路线：可信人物/发布者配置 → Search候选 → URL白名单 → Fetch原文 → 人物线索核验 → 缓存合并
// 联调关系：服务端持有Key和Store；begin/merge/fail刷新代次由仓储实现，Qt只展示资讯，不接收Key或任意抓取URL。
// 标有“答辩导读”的内容仅为讲解注释；原有条件、接口和算法不变。
// <<< DEFENSE-NOTE | 本段讲解结束
#include "deepseek_person_news.h"
#include "person_profile_lookup.h"
#include "person_store.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QUrlQuery>
#include <memory>
#include <utility>

using namespace care_contract;
using warehouse_contract::ErrorCode;

namespace deepseek_person_news {
namespace {
const QUrl SearchUrl(QStringLiteral("https://api.deepseek.com/v1/tools/search"));
const QUrl FetchUrl(QStringLiteral("https://api.deepseek.com/v1/tools/fetch"));
constexpr int SearchLimit = 6;
constexpr int FetchLimit = 3;
constexpr int TimeoutMs = 10000;
constexpr qint64 MaxResponseBytes = 2 * 1024 * 1024;

// >>> DEFENSE-NOTE | SearchPlan = 规范化检索计划
// 保存实际缓存来源标识、搜索词、核准域名、姓名和公开识别线索；source是搜索来源，不是文章地址。
// <<< DEFENSE-NOTE | 本段讲解结束
struct SearchPlan {
    QUrl source;
    QString query;
    QString site;
    QString name;
    QString clue;
};
// >>> DEFENSE-NOTE | Article = 待Fetch的候选文章
// 只保存文章URL和可能的发布日期；搜索结果摘要不是已经核实的原文。
// <<< DEFENSE-NOTE | 本段讲解结束
struct Article {
    QUrl source;
    QJsonValue publication;
};
struct Response {
    QByteArray bytes;
    bool oversized = false;
};

// >>> DEFENSE-NOTE | failure = 返回资讯服务错误
// 保留错误语义；无资讯、请求失败和缓存过期是不同状态。
// <<< DEFENSE-NOTE | 本段讲解结束
Status failure(const QString& message)
{
    return {ErrorCode::ServerError, message};
}

// >>> DEFENSE-NOTE | normalized = 兼容规范化与空白整理
// 使人物名字和识别线索能与网页文本进行一致的包含比较，不声称完成自然语言事实核验。
// <<< DEFENSE-NOTE | 本段讲解结束
QString normalized(const QString& text)
{
    return text.normalized(QString::NormalizationForm_KC).simplified();
}

// >>> DEFENSE-NOTE | queryTerm = 校验受控检索词
// 不允许引号、反斜杠、空字符或控制字符破坏固定查询结构。
// <<< DEFENSE-NOTE | 本段讲解结束
bool queryTerm(const QString& value)
{
    if (value.isEmpty())
        return false;
    for (const auto ch : value)
        if (ch == QLatin1Char('"') || ch == QLatin1Char('\x5c') || ch.isNull() ||
            ch.category() == QChar::Other_Control)
            return false;
    return true;
}

// >>> DEFENSE-NOTE | prepare = 验证人物与可信发布者并构造查询
// 核验人物ID、域名结构、不能为IP、姓名及识别线索；生成“姓名”“线索”site:域名。
// 限制查询长度，source把Search端点与site参数组合为缓存来源身份。
// <<< DEFENSE-NOTE | 本段讲解结束
Result<SearchPlan> prepare(const Request& request)
{
    const auto person = validatePersonId(request.personId);
    if (!person.ok())
        return {person, {}};
    SearchPlan plan;
    plan.name = normalized(request.displayName);
    plan.clue = normalized(request.identityClue);
    const auto site = request.approvedSite.trimmed();
    static const QRegularExpression hostname(
        QStringLiteral(R"(\A[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)+\z)"));
    const QUrl siteUrl(QStringLiteral("https://") + site, QUrl::StrictMode);
    plan.site = siteUrl.host().toLower();
    QHostAddress ip;
    if (!hostname.match(site).hasMatch() || !siteUrl.isValid() || plan.site.isEmpty() ||
        ip.setAddress(plan.site) || !queryTerm(plan.name) || !queryTerm(plan.clue))
        return {{ErrorCode::InvalidArgument, QStringLiteral("需要经核验的发布者域名和公开人物识别线索。")}, {}};
    plan.query = QStringLiteral("\"%1\" \"%2\" site:%3").arg(plan.name, plan.clue, plan.site);
    if (plan.query.size() > 128)
        return {{ErrorCode::InvalidArgument, QStringLiteral("资讯检索词超过 128 个字符。")}, {}};
    plan.source = SearchUrl;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("site"), plan.site);
    plan.source.setQuery(query);
    return {{}, std::move(plan)};
}

template<class Completion>
// >>> DEFENSE-NOTE | postJson = 受限异步HTTP工具
// 给固定API发JSON，携带服务器Key；禁止自动重定向。
// 单次超时与累计响应字节预算共同限制请求；只有HTTP200、无网络错误、合法JSON对象才回调有效结果。
// <<< DEFENSE-NOTE | 本段讲解结束
void postJson(QNetworkAccessManager& network, const QUrl& api, const QByteArray& key, const QJsonObject& body,
              Completion done)
{
    QNetworkRequest request(api);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", "Bearer " + key);
    request.setTransferTimeout(TimeoutMs);
    auto* reply = network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    reply->setReadBufferSize(MaxResponseBytes + 1);
    const auto data = std::make_shared<Response>();
    const auto read = [reply, data] {
        if (data->oversized)
            return;
        data->bytes.append(reply->read(MaxResponseBytes + 1 - data->bytes.size()));
        data->oversized = data->bytes.size() > MaxResponseBytes;
        if (data->oversized && !reply->isFinished())
            reply->abort();
    };
    QObject::connect(reply, &QNetworkReply::readyRead, reply, read);
    QTimer::singleShot(TimeoutMs, reply, [reply] {
        if (!reply->isFinished())
            reply->abort();
    });
    const auto complete = [reply, data, read, done = std::move(done)] {
        read();
        const auto code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        std::optional<QJsonObject> result;
        if (code == 200 && reply->error() == QNetworkReply::NoError && !data->oversized) {
            const auto document = QJsonDocument::fromJson(data->bytes);
            if (document.isObject())
                result = document.object();
        }
        reply->deleteLater();
        done(std::move(result));
    };
    QObject::connect(reply, &QNetworkReply::finished, reply, complete);
    if (reply->isFinished())
        complete();
}

// >>> DEFENSE-NOTE | Refresh = 一次人物资讯刷新状态机
// shared_from_this让异步链持有当前任务；保存候选、下一个索引和已验证资讯。
// search→collectCandidates→fetchNext→saveVerified，任何失败走failed更新缓存状态。
// <<< DEFENSE-NOTE | 本段讲解结束
class Refresh final : public std::enable_shared_from_this<Refresh> {
public:
    Refresh(person_store::Store& store, QNetworkAccessManager& network, SearchPlan plan, QString personId,
            QByteArray key, qint64 generation, StatusCallback done)
        : store_(store), network_(network), plan_(std::move(plan)), personId_(std::move(personId)),
          key_(std::move(key)), generation_(generation), done_(std::move(done))
    {
    }

    // >>> DEFENSE-NOTE | search = 请求Search候选
    // 仅向固定SearchUrl发送经过prepare限制的query，最多6条；回来的results仍须验证URL。
    // <<< DEFENSE-NOTE | 本段讲解结束
    void search()
    {
        postJson(network_, SearchUrl, key_,
                 {{"query", plan_.query}, {"count", SearchLimit}, {"need_content", false}},
                 [self = shared_from_this()](std::optional<QJsonObject> result) {
            if (!result || !result->value("results").isArray()) {
                self->failed(QStringLiteral("DeepSeek 资讯检索失败。"));
                return;
            }
            self->collectCandidates(result->value("results").toArray());
        });
    }

private:
    // >>> DEFENSE-NOTE | collectCandidates = 筛选可抓取文章
    // 只接受HTTPS、准确匹配已核准域名、默认443端口、无用户信息/fragment；URL去重，最多Fetch3篇。
    // 这不是允许客户端任意网址的网页代理。
    // <<< DEFENSE-NOTE | 本段讲解结束
    void collectCandidates(const QJsonArray& results)
    {
        if (results.size() > SearchLimit) {
            failed(QStringLiteral("DeepSeek 返回的资讯数量超出请求范围。"));
            return;
        }
        QSet<QUrl> seen;
        bool parsed = false;
        for (const auto& value : results) {
            const auto row = value.toObject();
            if (!row.value("url").isString())
                continue;
            const QUrl url(row.value("url").toString(), QUrl::StrictMode);
            parsed = parsed || url.isValid();
            if (!url.isValid() || url.scheme() != "https" || !url.userInfo().isEmpty() ||
                url.host() != plan_.site || url.port(443) != 443 || url.hasFragment() ||
                url.toEncoded().size() > 2048 || seen.contains(url))
                continue;
            seen.insert(url);
            candidates_.append({url, row.value("date")});
            if (candidates_.size() == FetchLimit)
                break;
        }
        if (!results.isEmpty() && !parsed) {
            failed(QStringLiteral("DeepSeek 返回的资讯链接无效。"));
            return;
        }
        fetchNext();
    }

    // >>> DEFENSE-NOTE | fetchNext = 串行抓取下一篇原文
    // 每次只Fetch一篇，完成后继续；全部处理完成才以同一generation合并缓存。
    // HTTP失败或正文缺失使本轮失败，不把空白正文当证据。
    // <<< DEFENSE-NOTE | 本段讲解结束
    void fetchNext()
    {
        if (next_ == candidates_.size()) {
            done_(store_.mergeUpdates(personId_, plan_.source, generation_,
                                      QDateTime::currentSecsSinceEpoch(), items_));
            return;
        }
        const auto article = candidates_.at(next_++);
        postJson(network_, FetchUrl, key_, {{"url", article.source.toString(QUrl::FullyEncoded)}},
                 [self = shared_from_this(), article](std::optional<QJsonObject> result) {
            if (!result || !result->value("title").isString() ||
                !result->value("content").isString() || result->value("content").toString().trimmed().isEmpty()) {
                self->failed(QStringLiteral("DeepSeek 未能读取原始资讯页面。"));
                return;
            }
            self->saveVerified(article, *result);
            self->fetchNext();
        });
    }

    // >>> DEFENSE-NOTE | saveVerified = 将通过最低证据检查的文章转缓存记录
    // 标题与原文合起来必须同时含人物名和公开身份线索；只是确定性的最低校验，不是绝对真实性证明。
    // updateId=文章URL的SHA-256稳定摘要，便于重复刷新去重；未知/未来/无时区的发表时间不编造。
    // <<< DEFENSE-NOTE | 本段讲解结束
    void saveVerified(const Article& article, const QJsonObject& page)
    {
        const auto title = page.value("title").toString().simplified();
        const auto evidence = normalized(title + QLatin1Char(' ') + page.value("content").toString());
        if (title.isEmpty() || title.size() > MaxQueryUnits ||
            !evidence.contains(plan_.name, Qt::CaseInsensitive) ||
            !evidence.contains(plan_.clue, Qt::CaseInsensitive))
            return;
        const auto now = QDateTime::currentSecsSinceEpoch();
        std::optional<qint64> publication;
        const auto date = QDateTime::fromString(article.publication.toString(), Qt::ISODate);
        if (date.isValid() && date.timeSpec() != Qt::LocalTime &&
            date.toSecsSinceEpoch() >= 0 && date.toSecsSinceEpoch() <= now)
            publication = date.toSecsSinceEpoch();
        const auto id = QStringLiteral("deepseek:") + QString::fromLatin1(
            QCryptographicHash::hash(article.source.toEncoded(), QCryptographicHash::Sha256).toHex());
        PersonUpdate update{id, personId_, title, UpdateKind::News, article.source,
                            publication, std::nullopt, now, false};
        if (validate(update).ok())
            items_.append(std::move(update));
    }

    // >>> DEFENSE-NOTE | failed = 标记本轮缓存刷新失败
    // 调用failUpdatesRefresh限定人物、来源和代次；已有缓存保留并标为stale，返回可解释错误。
    // <<< DEFENSE-NOTE | 本段讲解结束
    void failed(const QString& message)
    {
        const auto result = store_.failUpdatesRefresh(personId_, plan_.source, generation_);
        done_(result.ok() ? failure(message + QStringLiteral(" 已有缓存标为过期。")) : result);
    }

    person_store::Store& store_;
    QNetworkAccessManager& network_;
    SearchPlan plan_;
    QString personId_;
    QByteArray key_;
    qint64 generation_;
    StatusCallback done_;
    QList<Article> candidates_;
    QList<PersonUpdate> items_;
    qsizetype next_ = 0;
};
} // namespace

void findPeople(QNetworkAccessManager& network, const QString& name, const QByteArray& apiKey,
                Callback<QList<PersonWebSource>> done)
{
    auto config = llm_recommendation::loadConfig();
    config.apiKey = QString::fromUtf8(apiKey);
    person_profile_lookup::lookup(network, name, config, std::move(done));
}

// >>> DEFENSE-NOTE | cacheSource = 计算人物搜索缓存来源
// 复用prepare，得到含site参数的来源URL；不要与文章sourceUrl混用。
// <<< DEFENSE-NOTE | 本段讲解结束
Result<QUrl> cacheSource(const Request& request)
{
    const auto plan = prepare(request);
    return {plan.status, plan.value ? std::optional<QUrl>(plan.value->source) : std::nullopt};
}

// >>> DEFENSE-NOTE | isSearchSource = 判断是不是本模块的Search来源
// 去除查询部分后比较固定端点，再确认有site参数。
// <<< DEFENSE-NOTE | 本段讲解结束
bool isSearchSource(const QUrl& source)
{
    return source.adjusted(QUrl::RemoveQuery) == SearchUrl && QUrlQuery(source).hasQueryItem("site");
}

// >>> DEFENSE-NOTE | refresh = 对外启动人物资讯刷新
// 验证请求与Key格式，先beginUpdatesRefresh取得generation，再建立Refresh任务。
// 成功merge与失败fail都携带该generation，防旧请求覆盖新一轮缓存。
// <<< DEFENSE-NOTE | 本段讲解结束
void refresh(person_store::Store& store, QNetworkAccessManager& network, const Request& request,
             const QByteArray& apiKey, StatusCallback done)
{
    auto plan = prepare(request);
    if (!plan.status.ok()) {
        done(plan.status);
        return;
    }
    const auto key = apiKey.trimmed();
    if (key.isEmpty() || key.size() > 4096 || key.contains('\n') || key.contains('\r') || key.contains('\0')) {
        done({ErrorCode::InvalidArgument, QStringLiteral("请在服务器进程设置 MEDIA_SERVER_DEEPSEEK_API_KEY。")});
        return;
    }
    const auto generation = store.beginUpdatesRefresh(request.personId, plan.value->source);
    if (!generation.status.ok() || !generation.value) {
        done(generation.status.ok() ? failure(QStringLiteral("无法开始 DeepSeek 资讯刷新。")) : generation.status);
        return;
    }
    auto job = std::make_shared<Refresh>(store, network, std::move(*plan.value), request.personId,
                                         key, *generation.value, std::move(done));
    job->search();
}

} // namespace deepseek_person_news
