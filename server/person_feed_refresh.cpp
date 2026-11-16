#include "person_feed_refresh.h"
#include "person_store.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSet>
#include <QTimer>
#include <QXmlStreamReader>
#include <memory>
#include <utility>

using namespace care_contract;
using warehouse_contract::ErrorCode;

namespace person_feed_refresh {
namespace {
constexpr qint64 MaxFeedBytes = 2 * 1024 * 1024;
constexpr int MaxFeedEntries = 500;
constexpr int TimeoutMs = 15000;
const QString AtomNamespace = QStringLiteral("http://www.w3.org/2005/Atom");

Status failure(const QString& message)
{
    return {ErrorCode::ServerError, message};
}

bool httpsUrl(const QUrl& url)
{
    return url.isValid() && url.scheme() == "https" && !url.host().isEmpty() &&
           url.userInfo().isEmpty() && url.toString(QUrl::FullyEncoded).size() <= 2048;
}

bool readEntry(QXmlStreamReader& xml, bool atom, const QUrl& feed, const QString& personId,
               qint64 now, PersonUpdate& item)
{
    QString title, link, date;
    while (xml.readNextStartElement()) {
        const auto name = xml.name().toString();
        if (atom ? xml.namespaceUri() != AtomNamespace : !xml.namespaceUri().isEmpty()) {
            xml.skipCurrentElement();
        } else if (name == "title") {
            title = xml.readElementText(QXmlStreamReader::IncludeChildElements).simplified();
        } else if (name == "link" && atom) {
            const auto rel = xml.attributes().value("rel");
            if (link.isEmpty() && (rel.isEmpty() || rel == "alternate"))
                link = xml.attributes().value("href").toString();
            xml.skipCurrentElement();
        } else if (name == "link" && !atom) {
            link = xml.readElementText().trimmed();
        } else if (name == (atom ? QStringLiteral("published") : QStringLiteral("pubDate"))) {
            date = xml.readElementText().trimmed();
        } else {
            xml.skipCurrentElement();
        }
    }
    if (xml.hasError() || link.isEmpty())
        return false;
    const QUrl href(link, QUrl::StrictMode);
    if (!href.isValid())
        return false;
    auto article = feed.resolved(href);
    if (!httpsUrl(article))
        return false;
    article.setFragment(QString());
    const auto id = QStringLiteral("feed:") + QString::fromLatin1(
        QCryptographicHash::hash(article.toEncoded(), QCryptographicHash::Sha256).toHex());
    std::optional<qint64> published;
    if (!date.isEmpty()) {
        const auto parsed = QDateTime::fromString(date, atom ? Qt::ISODate : Qt::RFC2822Date);
        if (!parsed.isValid() || parsed.timeSpec() == Qt::LocalTime || parsed.toSecsSinceEpoch() > now)
            return false;
        published = parsed.toSecsSinceEpoch();
    }
    item = {id, personId, title, UpdateKind::News, article, published, std::nullopt, now, false};
    return title.size() <= MaxQueryUnits && validate(item).ok();
}

std::optional<QList<PersonUpdate>> parseFeed(const QByteArray& body, const person_source_config::Source& source,
                                            qint64 now)
{
    QXmlStreamReader xml(body);
    while (!xml.atEnd() && !xml.isStartElement()) {
        if (xml.readNext() == QXmlStreamReader::DTD)
            return std::nullopt;
    }
    const bool atom = xml.name() == "feed" && xml.namespaceUri() == AtomNamespace;
    const bool rss = xml.name() == "rss" && xml.namespaceUri().isEmpty();
    if (!atom && !rss)
        return std::nullopt;
    if (rss && (!xml.readNextStartElement() || xml.name() != "channel" || !xml.namespaceUri().isEmpty()))
        return std::nullopt;

    QList<PersonUpdate> items;
    QSet<QString> seen;
    int entries = 0;
    while (xml.readNextStartElement()) {
        const bool entry = atom ? xml.name() == "entry" && xml.namespaceUri() == AtomNamespace
                                : xml.name() == "item" && xml.namespaceUri().isEmpty();
        if (!entry) {
            xml.skipCurrentElement();
            continue;
        }
        PersonUpdate item;
        if (++entries > MaxFeedEntries || !readEntry(xml, atom, source.feedUrl, source.personId, now, item))
            return std::nullopt;
        if (!seen.contains(item.updateId)) {
            seen.insert(item.updateId);
            items.append(std::move(item));
        }
    }
    // Detect malformed trailing XML and a second root, including after an RSS channel.
    while (!xml.atEnd())
        xml.readNext();
    if (xml.hasError())
        return std::nullopt;
    return items;
}
} // namespace

void refresh(person_store::Store& store, QNetworkAccessManager& network,
             const person_source_config::Source& source, StatusCallback done)
{
    if (!httpsUrl(source.feedUrl) || source.feedUrl.hasFragment()) {
        done({ErrorCode::InvalidArgument, QStringLiteral("资讯源必须是已登记的无片段 HTTPS 地址。")});
        return;
    }
    const auto generation = store.beginUpdatesRefresh(source.personId, source.feedUrl);
    if (!generation.status.ok() || !generation.value) {
        done(generation.status.ok() ? failure(QStringLiteral("无法建立资讯刷新。")) : generation.status);
        return;
    }
    QNetworkRequest request(source.feedUrl);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setRawHeader("Accept", "application/atom+xml, application/rss+xml, application/xml, text/xml");
    request.setTransferTimeout(TimeoutMs);
    auto* reply = network.get(request);
    reply->setReadBufferSize(MaxFeedBytes + 1);
    struct Download { QByteArray content; bool overLimit = false; };
    const auto state = std::make_shared<Download>();
    const auto collect = [reply, state] {
        if (state->overLimit)
            return;
        state->content.append(reply->read(MaxFeedBytes + 1 - state->content.size()));
        state->overLimit = state->content.size() > MaxFeedBytes;
        if (state->overLimit && !reply->isFinished())
            reply->abort();
    };
    QObject::connect(reply, &QNetworkReply::readyRead, reply, collect);
    QTimer::singleShot(TimeoutMs, reply, [reply] {
        if (!reply->isFinished())
            reply->abort();
    });
    const auto complete =
        [reply, state, source, collect, &store, generation = *generation.value, done = std::move(done)] {
            collect();
            const auto now = QDateTime::currentSecsSinceEpoch();
            const bool downloaded = !state->overLimit && reply->error() == QNetworkReply::NoError &&
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
            auto items = downloaded ? parseFeed(state->content, source, now) : std::nullopt;
            reply->deleteLater();
            if (!items) {
                const auto saved = store.failUpdatesRefresh(source.personId, source.feedUrl, generation);
                done(saved.ok() ? failure(QStringLiteral("资讯刷新失败，已保留上次可用缓存。")) : saved);
                return;
            }
            done(store.mergeUpdates(source.personId, source.feedUrl, generation, now, *items));
        };
    QObject::connect(reply, &QNetworkReply::finished, reply, complete);
    if (reply->isFinished())
        complete();
}

} // namespace person_feed_refresh
