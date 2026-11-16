#include "assistant_dialog.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

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

AssistantDialog::AssistantDialog(QWidget* parent,
                                 std::function<void(const QString&, media_mood::Mood, const QString&)> openSearch,
                                 std::function<void()> openRecommendations, const QString& recommendationLabel)
    : QDialog(parent)
{
    using media_mood::Mood;
    setWindowTitle(QStringLiteral("媒体助手"));
    setWindowIcon(assistantIcon());
    resize(530, 320);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(22, 20, 22, 20);
    layout->setSpacing(12);
    auto* heading = new QHBoxLayout;
    auto* mascot = new QLabel;
    mascot->setPixmap(assistantIcon().pixmap(42, 42));
    auto* title = new QLabel(QStringLiteral("今天想找什么样的内容？"));
    title->setObjectName(QStringLiteral("dialogTitle"));
    heading->addWidget(mascot);
    heading->addWidget(title, 1);
    layout->addLayout(heading);
    auto* description = new QLabel(QStringLiteral("选一个想要的内容氛围，也可以按片名查找。氛围标签目前覆盖部分样例。"));
    description->setObjectName(QStringLiteral("muted"));
    description->setWordWrap(true);
    layout->addWidget(description);

    auto* form = new QFormLayout;
    auto* mood = new QComboBox;
    mood->setObjectName(QStringLiteral("assistantMood"));
    for (auto choice : {Mood::Any, Mood::Relaxing, Mood::Resonant, Mood::Energizing, Mood::Nostalgic})
        mood->addItem(media_mood::label(choice), static_cast<int>(choice));
    form->addRow(QStringLiteral("内容氛围"), mood);

    auto* category = new QComboBox;
    category->setObjectName(QStringLiteral("assistantCategory"));
    category->addItem(QStringLiteral("全部内容"), QString());
    for (const auto& value : {QStringLiteral("音乐"), QStringLiteral("电影"), QStringLiteral("电视剧")})
        category->addItem(value, value);
    form->addRow(QStringLiteral("内容类别"), category);

    auto* keyword = new QLineEdit;
    keyword->setObjectName(QStringLiteral("assistantMediaKeyword"));
    keyword->setPlaceholderText(QStringLiteral("可选"));
    keyword->setMaxLength(200);
    form->addRow(QStringLiteral("片名关键词"), keyword);
    layout->addLayout(form);
    layout->addStretch();

    auto* actions = new QHBoxLayout;
    auto* recommendations = new QPushButton(recommendationLabel);
    auto* search = new QPushButton(QStringLiteral("查本地媒体"));
    search->setObjectName(QStringLiteral("primary"));
    search->setAutoDefault(false);
    recommendations->setAutoDefault(false);
    actions->addWidget(recommendations);
    actions->addStretch();
    actions->addWidget(search);
    layout->addLayout(actions);

    const auto findMedia = [this, keyword, mood, category, openSearch] {
        hide();
        openSearch(keyword->text().trimmed(), static_cast<Mood>(mood->currentData().toInt()),
                   category->currentData().toString());
    };
    connect(search, &QPushButton::clicked, this, findMedia);
    connect(keyword, &QLineEdit::returnPressed, this, findMedia);
    connect(recommendations, &QPushButton::clicked, this, [this, openRecommendations] {
        hide();
        openRecommendations();
    });
}
