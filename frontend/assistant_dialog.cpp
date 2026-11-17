#include "assistant_dialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QSettings>
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
                                 std::function<void(const QString&, media_mood::Mood, const QString&, bool)> openSearch,
                                 std::function<void(warm_mode::Scene)> changeScene,
                                 std::function<void()> openRecommendations, const QString& recommendationLabel)
    : QDialog(parent)
{
    using media_mood::Mood;
    setWindowTitle(QStringLiteral("媒体助手"));
    setWindowIcon(assistantIcon());
    resize(580, 425);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(22, 20, 22, 20);
    layout->setSpacing(12);
    auto* sceneCard = new QFrame;
    sceneCard->setObjectName(QStringLiteral("warmSceneCard"));
    auto* sceneLayout = new QVBoxLayout(sceneCard);
    sceneLayout->setContentsMargins(16, 14, 16, 14);
    auto* heading = new QHBoxLayout;
    auto* mascot = new QLabel;
    mascot->setPixmap(assistantIcon().pixmap(42, 42));
    auto* title = new QLabel(QStringLiteral("今天想找什么样的内容？"));
    title->setObjectName(QStringLiteral("dialogTitle"));
    heading->addWidget(mascot);
    heading->addWidget(title, 1);
    sceneLayout->addLayout(heading);
    auto* description = new QLabel;
    description->setObjectName(QStringLiteral("muted"));
    description->setWordWrap(true);
    sceneLayout->addWidget(description);

    using warm_mode::Scene;
    constexpr auto settingsKey = "warmMode/animationEnabled";
    auto* settings = new QSettings(QSettings::IniFormat, QSettings::UserScope,
                                   QStringLiteral("MediaSearchSystem"), QStringLiteral("WarmMode"), this);
    auto* animationEnabled = new QCheckBox(QStringLiteral("启用场景动效"));
    animationEnabled->setObjectName(QStringLiteral("warmAnimationEnabled"));
    animationEnabled->setChecked(settings->value(settingsKey, true).toBool());
    auto* opacity = new QGraphicsOpacityEffect(mascot);
    opacity->setOpacity(1.0);
    mascot->setGraphicsEffect(opacity);
    auto* animation = new QPropertyAnimation(opacity, "opacity", this);
    animation->setObjectName(QStringLiteral("warmSceneAnimation"));
    animation->setDuration(200);
    animation->setStartValue(0.45);
    animation->setEndValue(1.0);
    connect(animationEnabled, &QCheckBox::toggled, this, [=](bool enabled) {
        settings->setValue(settingsKey, enabled);
        if (!enabled) {
            animation->stop();
            opacity->setOpacity(1.0);
        }
    });

    auto* scenes = new QButtonGroup(this);
    auto* choices = new QHBoxLayout;
    for (auto scene : {Scene::Neutral, Scene::Relax, Scene::Focus, Scene::Explore}) {
        auto* button = new QPushButton(warm_mode::theme(scene).label);
        button->setObjectName(QStringLiteral("warmSceneButton"));
        button->setCheckable(true);
        button->setAutoDefault(false);
        scenes->addButton(button, static_cast<int>(scene));
        choices->addWidget(button, 1);
    }
    sceneLayout->addLayout(choices);
    sceneLayout->addWidget(animationEnabled);
    layout->addWidget(sceneCard);

    auto* form = new QFormLayout;
    auto* mood = new QComboBox;
    mood->setObjectName(QStringLiteral("assistantMood"));
    constexpr int followScene = -1;
    mood->addItem(QStringLiteral("按场景"), followScene);
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
    form->addRow(QStringLiteral("关键词"), keyword);
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

    const auto applyScene = [=](int id) {
        const auto scene = static_cast<Scene>(id);
        const auto theme = warm_mode::theme(scene);
        sceneCard->setStyleSheet(QStringLiteral("QFrame#warmSceneCard { background: %1; border-radius: 12px; }"
                                               "QPushButton#warmSceneButton:checked { background: %2; color: white; }")
                                      .arg(theme.surface, theme.banner));
        const bool hasMood = theme.mood != Mood::Any;
        mood->setItemText(0, hasMood ? QStringLiteral("按场景 · %1").arg(media_mood::label(theme.mood))
                                    : QStringLiteral("按场景 · 不限氛围"));
        description->setText(hasMood
            ? QStringLiteral("放松场景可按轻松氛围查找。氛围标签目前覆盖部分样例。")
            : QStringLiteral("也可以手动选择内容氛围。氛围标签目前覆盖部分样例。"));
        animation->stop();
        opacity->setOpacity(1.0);
        if (isVisible() && animationEnabled->isChecked())
            animation->start();
        changeScene(scene);
    };
    connect(scenes, &QButtonGroup::idToggled, this, [applyScene](int id, bool checked) {
        if (checked)
            applyScene(id);
    });
    scenes->button(static_cast<int>(Scene::Neutral))->setChecked(true);

    const auto findMedia = [this, keyword, mood, category, scenes, openSearch] {
        hide();
        const int selectedMood = mood->currentData().toInt();
        const bool useScene = selectedMood == followScene;
        const auto effectiveMood = useScene ? warm_mode::theme(static_cast<Scene>(scenes->checkedId())).mood
                                            : static_cast<Mood>(selectedMood);
        openSearch(keyword->text().trimmed(), effectiveMood, category->currentData().toString(), useScene);
    };
    connect(search, &QPushButton::clicked, this, findMedia);
    connect(keyword, &QLineEdit::returnPressed, this, findMedia);
    connect(recommendations, &QPushButton::clicked, this, [this, openRecommendations] {
        hide();
        openRecommendations();
    });
}
