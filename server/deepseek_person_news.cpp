#include "deepseek_person_news.h"
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

struct SearchPlan {
    QUrl source;
    QString query;
    QString site;
    QString name;
    QString clue;
};
struct Article {
    QUrl source;
    QJsonValue publication;
};
struct Response {
    QByteArray bytes;
    bool oversized = false;
};

Status failure(const QString& message)
{
    return {ErrorCode::ServerError, message};
}

QString normalized(const QString& text)
{
    return text.normalized(QString::NormalizationForm_KC).simplified();
}

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

class Refresh final : public std::enable_shared_from_this<Refresh> {
public:
    Refresh(person_store::Store& store, QNetworkAccessManager& network, SearchPlan plan, QString personId,
            QByteArray key, qint64 generation, StatusCallback done)
        : store_(store), network_(network), plan_(std::move(plan)), personId_(std::move(personId)),
          key_(std::move(key)), generation_(generation), done_(std::move(done))
    {
    }

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
    const auto term = normalized(name);
    const auto query = QStringLiteral("\"%1\" site:catalog.afi.com OR site:musicbrainz.org OR site:tvmaze.com").arg(term);
    if (!queryTerm(term) || query.toUcs4().size() > 128) {
        done({{ErrorCode::InvalidArgument, QStringLiteral("请输入较短的人物姓名。")}, {}});
        return;
    }
    const auto key = apiKey.trimmed();
    if (key.isEmpty() || key.size() > 4096 || key.contains('\n') || key.contains('\r') || key.contains('\0')) {
        done({failure(QStringLiteral("服务器尚未配置 DeepSeek 联网搜索。")), {}});
        return;
    }
    postJson(network, SearchUrl, key, {{"query", query}, {"count", 8}, {"need_content", false}},
        [&network, key, term, done = std::move(done)](std::optional<QJsonObject> reply) {
            if (!reply || !reply->value("results").isArray()) {
                done({failure(QStringLiteral("DeepSeek 人物搜索暂时失败，请重试。")), {}});
                return;
            }
            const auto rows = reply->value("results").toArray();
            for (const auto& row : rows) {
                const QUrl url(row.toObject().value("url").toString(), QUrl::StrictMode);
                if (!url.isValid() || url.scheme() != "https" || !url.userInfo().isEmpty() ||
                    url.port() != -1 || url.toEncoded().size() > 2048)
                    continue;
                const auto host = url.host();
                const auto path = url.path();
                if (!((host == "catalog.afi.com" && path.startsWith("/Catalog/PersonDetails/", Qt::CaseInsensitive)) ||
                      (host == "musicbrainz.org" && path.startsWith("/artist/")) ||
                      ((host == "www.tvmaze.com" || host == "tvmaze.com") && path.startsWith("/people/"))))
                    continue;
                const auto source = url.adjusted(QUrl::RemoveFragment | QUrl::RemoveQuery);
                postJson(network, FetchUrl, key, {{"url", source.toString(QUrl::FullyEncoded)}},
                    [source, term, done = std::move(done)](std::optional<QJsonObject> page) {
                        if (!page || !page->value("content").isString()) {
                            done({failure(QStringLiteral("网页原文暂时无法读取，请重试。")), {}});
                            return;
                        }
                        const auto content = normalized(page->value("content").toString());
                        QList<PersonWebSource> sources;
                        if (content.contains(term, Qt::CaseInsensitive)) {
                            const auto title = normalized(page->value("title").toString()).left(200);
                            sources.append({title.isEmpty() ? term : title, source});
                        }
                        done({{}, std::move(sources)});
                    });
                return;
            }
            done({{}, QList<PersonWebSource>{}});
        });
}

Result<QUrl> cacheSource(const Request& request)
{
    const auto plan = prepare(request);
    return {plan.status, plan.value ? std::optional<QUrl>(plan.value->source) : std::nullopt};
}

bool isSearchSource(const QUrl& source)
{
    return source.adjusted(QUrl::RemoveQuery) == SearchUrl && QUrlQuery(source).hasQueryItem("site");
}

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
