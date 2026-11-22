// >>> MAIN-DEFENSE | 主逻辑文件导读
// RSS/Atom资讯刷新：按已登记的HTTPS源抓取和解析，限制响应与时间，使用begin/merge/fail及generation维护缓存。
// 阅读时先看函数入口说明，再按“处理阶段”往下；到每个return核对成功、空结果和错误是否区别。
// <<< MAIN-DEFENSE
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

// >>> MAIN-DEFENSE | 函数入口｜failure
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：Status failure(const QString& message)  
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
Status failure(const QString& message)
{
    return {ErrorCode::ServerError, message};
}

// >>> MAIN-DEFENSE | 函数入口｜httpsUrl
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：bool httpsUrl(const QUrl& url)  
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool httpsUrl(const QUrl& url)
{
    return url.isValid() && url.scheme() == "https" && !url.host().isEmpty() &&
           url.userInfo().isEmpty() && url.toString(QUrl::FullyEncoded).size() <= 2048;
}

// >>> MAIN-DEFENSE | 函数入口｜readEntry
// 读取一条RSS/Atom内容，核对标题、链接和时间，未知日期保持缺失。
// 签名提示：bool readEntry(QXmlStreamReader& xml, bool atom, const QUrl& feed, const QString& personId,                 qint64 now, PersonUpdate& item)  
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
bool readEntry(QXmlStreamReader& xml, bool atom, const QUrl& feed, const QString& personId,
               qint64 now, PersonUpdate& item)
{
    QString title, link, date;
    // >>> MAIN-DEFENSE | readEntry｜处理阶段 01
    // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
    // <<< MAIN-DEFENSE
    while (xml.readNextStartElement()) {
        const auto name = xml.name().toString();
        if (atom ? xml.namespaceUri() != AtomNamespace : !xml.namespaceUri().isEmpty()) {
            xml.skipCurrentElement();
        } else if (name == "title") {
            title = xml.readElementText(QXmlStreamReader::IncludeChildElements).simplified();
        } else if (name == "link" && atom) {
            const auto rel = xml.attributes().value("rel");
            // >>> MAIN-DEFENSE | readEntry｜处理阶段 02
            // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
            // <<< MAIN-DEFENSE
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
    // >>> MAIN-DEFENSE | readEntry｜处理阶段 03
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (xml.hasError() || link.isEmpty())
        return false;
    const QUrl href(link, QUrl::StrictMode);
    if (!href.isValid())
        // >>> MAIN-DEFENSE | readEntry｜处理阶段 04
        // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
        // <<< MAIN-DEFENSE
        return false;
    auto article = feed.resolved(href);
    if (!httpsUrl(article))
        return false;
    article.setFragment(QString());
    const auto id = QStringLiteral("feed:") + QString::fromLatin1(
        QCryptographicHash::hash(article.toEncoded(), QCryptographicHash::Sha256).toHex());
    std::optional<qint64> published;
    // >>> MAIN-DEFENSE | readEntry｜处理阶段 05
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!date.isEmpty()) {
        const auto parsed = QDateTime::fromString(date, atom ? Qt::ISODate : Qt::RFC2822Date);
        if (!parsed.isValid() || parsed.timeSpec() == Qt::LocalTime || parsed.toSecsSinceEpoch() > now)
            return false;
        published = parsed.toSecsSinceEpoch();
    }
    item = {id, personId, title, UpdateKind::News, article, published, std::nullopt, now, false};
    // >>> MAIN-DEFENSE | readEntry｜处理阶段 06
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return title.size() <= MaxQueryUnits && validate(item).ok();
}

// >>> MAIN-DEFENSE | 函数入口｜parseFeed
// 将受限RSS/Atom正文解析成资讯列表，外部文本只作为数据，不当指令。
// 签名提示：std::optional<QList<PersonUpdate>> parseFeed(const QByteArray& body, const person_source_config::Source& source,                                              qint64 now)  
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
std::optional<QList<PersonUpdate>> parseFeed(const QByteArray& body, const person_source_config::Source& source,
                                            qint64 now)
{
    QXmlStreamReader xml(body);
    // >>> MAIN-DEFENSE | parseFeed｜处理阶段 01
    // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
    // <<< MAIN-DEFENSE
    while (!xml.atEnd() && !xml.isStartElement()) {
        if (xml.readNext() == QXmlStreamReader::DTD)
            return std::nullopt;
    }
    const bool atom = xml.name() == "feed" && xml.namespaceUri() == AtomNamespace;
    const bool rss = xml.name() == "rss" && xml.namespaceUri().isEmpty();
    // >>> MAIN-DEFENSE | parseFeed｜处理阶段 02
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!atom && !rss)
        return std::nullopt;
    if (rss && (!xml.readNextStartElement() || xml.name() != "channel" || !xml.namespaceUri().isEmpty()))
        return std::nullopt;

    QList<PersonUpdate> items;
    // >>> MAIN-DEFENSE | parseFeed｜处理阶段 03
    // 准备本步骤的内存查找、去重或汇总容器；它们不是数据库表，生命周期通常只在本次调用内。
    // <<< MAIN-DEFENSE
    QSet<QString> seen;
    int entries = 0;
    while (xml.readNextStartElement()) {
        const bool entry = atom ? xml.name() == "entry" && xml.namespaceUri() == AtomNamespace
                                : xml.name() == "item" && xml.namespaceUri().isEmpty();
        // >>> MAIN-DEFENSE | parseFeed｜处理阶段 04
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!entry) {
            xml.skipCurrentElement();
            continue;
        }
        PersonUpdate item;
        // >>> MAIN-DEFENSE | parseFeed｜处理阶段 05
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (++entries > MaxFeedEntries || !readEntry(xml, atom, source.feedUrl, source.personId, now, item))
            return std::nullopt;
        if (!seen.contains(item.updateId)) {
            seen.insert(item.updateId);
            items.append(std::move(item));
        }
    }
    // Detect malformed trailing XML and a second root, including after an RSS channel.
    // >>> MAIN-DEFENSE | parseFeed｜处理阶段 06
    // 进入批量处理：逐项处理下方集合或查询结果；每次迭代的continue/return决定跳过一项还是终止整个函数。
    // <<< MAIN-DEFENSE
    while (!xml.atEnd())
        xml.readNext();
    if (xml.hasError())
        return std::nullopt;
    // >>> MAIN-DEFENSE | parseFeed｜处理阶段 07
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return items;
}
} // namespace

// >>> MAIN-DEFENSE | 函数入口｜refresh
// 读取已登记来源，抓取解析后按generation合并；失败标记旧缓存过期。
// 签名提示：void refresh(person_store::Store& store, QNetworkAccessManager& network,               const person_source_config::Source& source, StatusCallback done)  
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
void refresh(person_store::Store& store, QNetworkAccessManager& network,
             const person_source_config::Source& source, StatusCallback done)
{
    if (!httpsUrl(source.feedUrl) || source.feedUrl.hasFragment()) {
        done({ErrorCode::InvalidArgument, QStringLiteral("资讯源必须是已登记的无片段 HTTPS 地址。")});
        return;
    }
    const auto generation = store.beginUpdatesRefresh(source.personId, source.feedUrl);
    // >>> MAIN-DEFENSE | refresh｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
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
        // >>> MAIN-DEFENSE | refresh｜处理阶段 02
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (state->overLimit)
            return;
        state->content.append(reply->read(MaxFeedBytes + 1 - state->content.size()));
        state->overLimit = state->content.size() > MaxFeedBytes;
        // >>> MAIN-DEFENSE | refresh｜处理阶段 03
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (state->overLimit && !reply->isFinished())
            reply->abort();
    };
    QObject::connect(reply, &QNetworkReply::readyRead, reply, collect);
    QTimer::singleShot(TimeoutMs, reply, [reply] {
        // >>> MAIN-DEFENSE | refresh｜处理阶段 04
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
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
            // >>> MAIN-DEFENSE | refresh｜处理阶段 05
            // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
            // <<< MAIN-DEFENSE
            if (!items) {
                const auto saved = store.failUpdatesRefresh(source.personId, source.feedUrl, generation);
                done(saved.ok() ? failure(QStringLiteral("资讯刷新失败，已保留上次可用缓存。")) : saved);
                return;
            }
            done(store.mergeUpdates(source.personId, source.feedUrl, generation, now, *items));
        };
    // >>> MAIN-DEFENSE | refresh｜处理阶段 06
    // 注册异步信号回调：这里只登记后续要执行的处理，网络完成时才运行lambda，不是在此同步等结果。
    // <<< MAIN-DEFENSE
    QObject::connect(reply, &QNetworkReply::finished, reply, complete);
    if (reply->isFinished())
        complete();
}

} // namespace person_feed_refresh
