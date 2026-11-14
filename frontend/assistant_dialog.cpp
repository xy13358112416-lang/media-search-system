#include "assistant_dialog.h"

#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPushButton>
#include <QTextCursor>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

namespace {

QJsonObject message(const QString& role, const QString& content)
{
    return {{QStringLiteral("role"), role}, {QStringLiteral("content"), content}};
}

QString requestError(const QNetworkReply& reply)
{
    switch (reply.attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()) {
    case 401:
    case 403: return QStringLiteral("DeepSeek 拒绝了请求，请检查 API 密钥和权限。");
    case 402: return QStringLiteral("DeepSeek 账户余额不足。");
    case 429: return QStringLiteral("请求过于频繁，请稍后重试。");
    default: return QStringLiteral("连接 DeepSeek 失败，请检查网络后重试。");
    }
}

} // namespace

QIcon assistantIcon()
{
    QPixmap artwork(80, 80);
    artwork.fill(Qt::transparent);
    QPainter painter(&artwork);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(QStringLiteral("#244f91")));
    painter.drawEllipse(QRectF(0, 0, 80, 80));
    painter.setBrush(QColor(QStringLiteral("#dceaff")));
    painter.drawRoundedRect(QRectF(16, 26, 48, 29), 12, 12);
    painter.drawRoundedRect(QRectF(25, 59, 30, 10), 5, 5);
    painter.drawRect(QRectF(38, 18, 4, 9));
    painter.drawEllipse(QRectF(35, 10, 10, 10));
    painter.setBrush(QColor(QStringLiteral("#244f91")));
    painter.drawEllipse(QRectF(27, 36, 7, 7));
    painter.drawEllipse(QRectF(46, 36, 7, 7));
    return QIcon(artwork);
}

AssistantDialog::AssistantDialog(QWidget* parent, std::function<void(const QString&)> openSearch,
                                 std::function<void()> openRecommendations, const QString& recommendationLabel)
    : QDialog(parent), network_(new QNetworkAccessManager(this)), key_(new QLineEdit),
      input_(new QLineEdit), send_(new QPushButton(QStringLiteral("发送"))),
      reset_(new QPushButton(QStringLiteral("新对话"))), status_(new QLabel),
      transcript_(new QTextEdit)
{
    setWindowTitle(QStringLiteral("媒体助手"));
    setWindowIcon(assistantIcon());
    resize(580, 520);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(22, 20, 22, 20);
    layout->setSpacing(10);
    auto* heading = new QHBoxLayout;
    auto* mascot = new QLabel;
    mascot->setPixmap(assistantIcon().pixmap(42, 42));
    auto* title = new QLabel(QStringLiteral("媒体助手"));
    title->setObjectName(QStringLiteral("dialogTitle"));
    heading->addWidget(mascot);
    heading->addWidget(title);
    heading->addStretch();
    heading->addWidget(reset_);
    layout->addLayout(heading);

    auto* shortcuts = new QHBoxLayout;
    auto* keyword = new QLineEdit;
    keyword->setObjectName(QStringLiteral("assistantMediaKeyword"));
    keyword->setAccessibleName(QStringLiteral("本地片名关键词"));
    keyword->setPlaceholderText(QStringLiteral("输入片名关键词，例如 晨光"));
    keyword->setMaxLength(200);
    auto* search = new QPushButton(QStringLiteral("查本地媒体"));
    auto* recommendations = new QPushButton(recommendationLabel);
    shortcuts->addWidget(keyword, 1);
    shortcuts->addWidget(search);
    shortcuts->addWidget(recommendations);
    layout->addLayout(shortcuts);
    search->setEnabled(false);
    connect(keyword, &QLineEdit::textChanged, search, [search](const QString& text) {
        search->setEnabled(!text.trimmed().isEmpty());
    });
    const auto findMedia = [this, keyword, openSearch] {
        const QString text = keyword->text().trimmed();
        if (text.isEmpty())
            return;
        hide();
        openSearch(text);
    };
    connect(search, &QPushButton::clicked, this, findMedia);
    connect(keyword, &QLineEdit::returnPressed, this, findMedia);
    connect(recommendations, &QPushButton::clicked, this, [this, openRecommendations] {
        hide();
        openRecommendations();
    });
    for (auto* button : {search, recommendations, send_, reset_})
        button->setAutoDefault(false);

    auto* keyRow = new QHBoxLayout;
    auto* keyLabel = new QLabel(QStringLiteral("DeepSeek API 密钥"));
    keyLabel->setBuddy(key_);
    key_->setObjectName(QStringLiteral("assistantApiKey"));
    key_->setEchoMode(QLineEdit::Password);
    key_->setPlaceholderText(QStringLiteral("也可通过 DEEPSEEK_API_KEY 配置"));
    key_->setText(qEnvironmentVariable("DEEPSEEK_API_KEY"));
    keyRow->addWidget(keyLabel);
    keyRow->addWidget(key_, 1);
    layout->addLayout(keyRow);
    auto* privacy = new QLabel(QStringLiteral("AI 对话会发送给 DeepSeek。上方片名检索在本机运行。"));
    privacy->setObjectName(QStringLiteral("muted"));
    privacy->setWordWrap(true);
    layout->addWidget(privacy);

    transcript_->setObjectName(QStringLiteral("assistantTranscript"));
    transcript_->setReadOnly(true);
    layout->addWidget(transcript_, 1);
    status_->setObjectName(QStringLiteral("assistantStatus"));
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    status_->hide();
    layout->addWidget(status_);

    auto* composer = new QHBoxLayout;
    input_->setObjectName(QStringLiteral("assistantInput"));
    input_->setAccessibleName(QStringLiteral("给媒体助手的问题"));
    input_->setPlaceholderText(QStringLiteral("例如：第一次使用，怎么按演员查作品？"));
    input_->setMaxLength(2000);
    send_->setObjectName(QStringLiteral("primary"));
    composer->addWidget(input_, 1);
    composer->addWidget(send_);
    layout->addLayout(composer);

    connect(send_, &QPushButton::clicked, this, [this] { send(); });
    connect(input_, &QLineEdit::returnPressed, this, [this] { send(); });
    connect(reset_, &QPushButton::clicked, this, [this] { showWelcome(); });
    showWelcome();
}

void AssistantDialog::showWelcome()
{
    history_ = {};
    transcript_->clear();
    status_->hide();
    input_->clear();
    addMessage(QStringLiteral("媒体助手"),
        QStringLiteral("你好！可以问我怎么检索、怎么使用今日推荐，也可以告诉我你喜欢什么类型，请我给你一些选片建议。"));
    input_->setFocus();
}

void AssistantDialog::addMessage(const QString& name, const QString& text)
{
    QTextCursor cursor(transcript_->document());
    cursor.movePosition(QTextCursor::End);
    if (!transcript_->document()->isEmpty()) {
        cursor.insertBlock();
        cursor.insertBlock();
    }
    QTextCharFormat author;
    author.setFontWeight(QFont::Bold);
    author.setForeground(QColor(QStringLiteral("#244f91")));
    cursor.insertText(name, author);
    cursor.insertBlock();
    QTextCharFormat body;
    body.setForeground(QColor(QStringLiteral("#182942")));
    cursor.insertText(text, body);
    transcript_->setTextCursor(cursor);
    transcript_->ensureCursorVisible();
}

void AssistantDialog::setBusy(bool busy)
{
    input_->setEnabled(!busy);
    key_->setEnabled(!busy);
    send_->setEnabled(!busy);
    reset_->setEnabled(!busy);
}

void AssistantDialog::send()
{
    if (!send_->isEnabled())
        return;
    const QString question = input_->text().trimmed();
    if (question.isEmpty())
        return;

    const QByteArray key = key_->text().trimmed().toUtf8();
    if (key.isEmpty()) {
        status_->setText(QStringLiteral("填入 DeepSeek API 密钥后即可发送。"));
        status_->show();
        key_->setFocus();
        return;
    }

    const QJsonObject userMessage = message(QStringLiteral("user"), question);
    QJsonArray messages{message(QStringLiteral("system"), QStringLiteral(
        "你是桌面多媒体文件管理及检索系统的媒体助手，使用简洁中文。"
        "检索页支持文件名关键词、导演全名、演员全名和文件类型；今日推荐可选择类别和文件类型。"
        "你可以解释界面、提供通用选片建议；你无法读取媒体库、账户资料和浏览历史，也无法操作程序。"
        "回答时不要声称已经查到本地结果或执行了操作。"))};
    for (const auto& previous : history_)
        messages.append(previous);
    messages.append(userMessage);

    const QJsonObject payload{
        {QStringLiteral("model"), QStringLiteral("deepseek-flash")},
        {QStringLiteral("messages"), messages},
        {QStringLiteral("thinking"), QJsonObject{{QStringLiteral("type"), QStringLiteral("disabled")}}},
        {QStringLiteral("max_tokens"), 800}
    };
    QNetworkRequest request(QUrl(QStringLiteral("https://api.deepseek.com/chat/completions")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QByteArray("Bearer ") + key);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    auto* reply = network_->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    QTimer::singleShot(45000, reply, [reply] {
        if (reply->isRunning())
            reply->abort();
    });

    setBusy(true);
    status_->setText(QStringLiteral("正在等待 DeepSeek 回复…"));
    status_->show();
    connect(reply, &QNetworkReply::finished, this, [this, reply, userMessage, question] {
        reply->deleteLater();
        setBusy(false);
        if (reply->error() != QNetworkReply::NoError ||
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) {
            status_->setText(reply->error() == QNetworkReply::OperationCanceledError
                ? QStringLiteral("DeepSeek 回复超时，可以再次发送。") : requestError(*reply));
            return;
        }

        const QJsonArray choices = QJsonDocument::fromJson(reply->readAll()).object()
            .value(QStringLiteral("choices")).toArray();
        const QString answer = choices.isEmpty() ? QString() : choices.at(0).toObject().value(QStringLiteral("message"))
            .toObject().value(QStringLiteral("content")).toString().trimmed();
        if (answer.isEmpty()) {
            status_->setText(QStringLiteral("DeepSeek 未返回可显示的回复，可以再次发送。"));
            return;
        }
        history_.append(userMessage);
        history_.append(message(QStringLiteral("assistant"), answer));
        if (history_.size() > 12) {
            history_.removeFirst();
            history_.removeFirst();
        }
        addMessage(QStringLiteral("你"), question);
        addMessage(QStringLiteral("媒体助手"), answer);
        input_->clear();
        input_->setFocus();
        status_->hide();
    });
}
