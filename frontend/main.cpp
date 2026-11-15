#include "medialibrary.h"
#include "warehouse_client.h"
#include "warehouse_page.h"
#include "mediarecommendation.h"
#include "library_style.h"
#include "account_dialog.h"
#include "avatar_button.h"
#include "assistant_dialog.h"
#include "care_dashboard.h"
#include "account_frontend.h"
#include "account_history_service.h"
#include "personalrecommendation_service.h"
#include "todayrecommendation_service.h"
#include "personalrecommendation_contract.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCompleter>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QFont>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QProgressBar>
#include <QGridLayout>
#include <algorithm>
#include <array>
#include <QScrollArea>
#include <QScopeGuard>
#include <QSet>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStringListModel>
#include <QTabBar>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QComboBox>

namespace {

QString displayText(const QString& value)
{
    return value.isEmpty() ? QStringLiteral("—") : value;
}

// Display a record's date using the same format in the list and the details.
QString displayDate(const QDate& date)
{
    return displayText(date.toString(Qt::ISODate));
}

// Split the form's list fields and discard empty entries.
QStringList splitEntries(const QString& text)
{
    QStringList entries;
    for (const auto& part : text.split(QChar(u'、'))) {
        const QString entry = part.trimmed();
        if (!entry.isEmpty())
            entries.append(entry);
    }
    return entries;
}

// Parse a nonnegative count without narrowing the supported 64-bit range.
bool readCount(const QString& text, quint64& value)
{
    const QString number = text.trimmed();
    bool ok = false;
    value = number.toULongLong(&ok);
    return ok && !number.startsWith(QLatin1Char('-'));
}

QString fieldName(SearchField field)
{
    switch (field) {
    case SearchField::Director: return QStringLiteral("导演");
    case SearchField::Actor: return QStringLiteral("演员");
    case SearchField::FileType: return QStringLiteral("文件类型");
    }
    return {};
}

// Display the recorded benchmark from retrieval/REPORT_NOTES.md.
void showPerformance(QWidget* parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("检索性能对照"));
    dialog.resize(720, 350);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 22, 24, 22);
    layout->setSpacing(12);

    auto* context = new QLabel(QStringLiteral(
        "10 万条合成记录 · 每种策略测量 31 次 · Windows 11 / Qt 6.8.3 / MinGW 13.1 / Release"));
    context->setObjectName(QStringLiteral("muted"));
    context->setWordWrap(true);
    layout->addWidget(context);

    struct Scenario {
        QString name;
        std::array<double, 3> timings; // Query medians in microseconds, in strategy order.
        QString candidates;           // First-step counts for fixed order and short list first.
    };
    const std::array<Scenario, 4> scenarios{{
        {QStringLiteral("稀有演员"), {9169.1, 2458.6, 2451.3}, QStringLiteral("50,163 → 101")},
        {QStringLiteral("稀有类型"), {12693.2, 4785.7, 2778.8}, QStringLiteral("50,163 → 101")},
        {QStringLiteral("热门条件"), {27571.0, 7676.5, 7428.9}, QStringLiteral("50,163 → 50,163")},
        {QStringLiteral("无结果"), {9206.7, 5.5, 1.6}, QStringLiteral("50,163 → 0")}
    }};
    const QStringList strategies{QStringLiteral("逐条扫描"), QStringLiteral("固定顺序索引"),
                                 QStringLiteral("短列表优先")};
    const QStringList styles{QStringLiteral("linearBar"), QStringLiteral("fixedBar"),
                             QStringLiteral("shortBar")};

    auto* selector = new QComboBox;
    for (const auto& scenario : scenarios)
        selector->addItem(scenario.name);
    auto* controls = new QHBoxLayout;
    auto* scenarioLabel = new QLabel(QStringLiteral("查询场景"));
    scenarioLabel->setBuddy(selector);
    controls->addWidget(scenarioLabel);
    controls->addWidget(selector);
    controls->addStretch();
    controls->addWidget(new QLabel(QStringLiteral("查询耗时中位数（µs）")));
    layout->addLayout(controls);

    auto* chart = new QGridLayout;
    chart->setHorizontalSpacing(12);
    chart->setVerticalSpacing(12);
    chart->setColumnStretch(1, 1);
    std::array<QProgressBar*, 3> bars{};
    std::array<QLabel*, 3> values{};
    for (int row = 0; row < static_cast<int>(bars.size()); ++row) {
        bars[row] = new QProgressBar;
        bars[row]->setObjectName(styles.at(row));
        bars[row]->setRange(0, 10000);
        bars[row]->setTextVisible(false);
        values[row] = new QLabel;
        values[row]->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        chart->addWidget(new QLabel(strategies.at(row)), row, 0);
        chart->addWidget(bars[row], row, 1);
        chart->addWidget(values[row], row, 2);
    }
    layout->addLayout(chart);

    auto* candidates = new QLabel;
    candidates->setObjectName(QStringLiteral("resultCount"));
    layout->addWidget(candidates, 0, Qt::AlignLeft);
    auto* note = new QLabel(QStringLiteral(
        "同一场景的条长使用相同比例。索引构建约 162 ms。数据来源：retrieval/REPORT_NOTES.md。"));
    note->setObjectName(QStringLiteral("muted"));
    note->setWordWrap(true);
    layout->addWidget(note);
    layout->addStretch();

    const auto refresh = [&](int index) {
        const auto& scenario = scenarios.at(index);
        const auto largest = *std::max_element(scenario.timings.begin(), scenario.timings.end());
        for (std::size_t i = 0; i < bars.size(); ++i) {
            const auto timing = scenario.timings[i];
            bars[i]->setValue(qRound(timing / largest * bars[i]->maximum()));
            values[i]->setText(QLocale().toString(timing, 'f', 1));
            bars[i]->setAccessibleName(strategies.at(i));
            bars[i]->setAccessibleDescription(values[i]->text() + QStringLiteral(" µs"));
        }
        candidates->setText(QStringLiteral("首步候选数（固定顺序 → 短列表优先）  %1")
                                .arg(scenario.candidates));
    };
    QObject::connect(selector, &QComboBox::currentIndexChanged, &dialog, refresh);
    refresh(selector->currentIndex());

    auto* close = new QPushButton(QStringLiteral("关闭"));
    layout->addWidget(close, 0, Qt::AlignRight);
    QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.exec();
}

class LibraryWindow final : public QWidget {
public:
    explicit LibraryWindow(MediaLibrary& library,
                           account_contract::AccountHistoryService* accounts = nullptr,
                           recommendation_contract::PersonalRecommendationService* recommendations = nullptr,
                           recommendation_contract::TodayRecommendationService* today = nullptr,
                           const QString& accountDatabasePath = {})
        : library_(library), accounts_(accounts), recommendations_(recommendations), today_(today),
          identity_(accounts ? accounts->currentUser() : std::nullopt)
    {
        setWindowTitle(QStringLiteral("多媒体文件管理及检索系统"));
        setObjectName(QStringLiteral("libraryWindow"));
        resize(1120, 710);
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(24, 22, 24, 24);
        layout->setSpacing(14);

        auto* banner = new QFrame;
        banner->setObjectName(QStringLiteral("banner"));
        auto* bannerLayout = new QHBoxLayout(banner);
        bannerLayout->setContentsMargins(20, 16, 20, 16);
        auto* heading = new QLabel(QStringLiteral("多媒体文件管理及检索系统"));
        heading->setObjectName(QStringLiteral("appTitle"));
        bannerLayout->addWidget(heading);
        bannerLayout->addStretch();
        if (identity_) {
            auto* assistant = new QPushButton(assistantIcon(), QStringLiteral("媒体助手"));
            assistant->setIconSize(QSize(22, 22));
            assistant->setObjectName(QStringLiteral("openAssistant"));
            bannerLayout->addWidget(assistant);
            auto* careDemo = new QPushButton(QStringLiteral("关怀演示"));
            bannerLayout->addWidget(careDemo);
            connect(careDemo, &QPushButton::clicked, this, [this] {
                using care_contract::BrowseScene;
                BrowseScene scene = BrowseScene::Neutral;
                switch (warmScene_) {
                case warm_mode::Scene::Neutral: break;
                case warm_mode::Scene::Relax: scene = BrowseScene::Relax; break;
                case warm_mode::Scene::Focus: scene = BrowseScene::Focus; break;
                case warm_mode::Scene::Explore: scene = BrowseScene::Explore; break;
                }
                showCareDemonstration(this, scene);
            });
            connect(assistant, &QPushButton::clicked, this, [this, banner] {
                if (!assistant_) {
                    assistant_ = new AssistantDialog(this, [this](const QString& keyword,
                                                       media_mood::Mood mood, const QString& category, bool useScene) {
                        sceneSearch_ = useScene;
                        director_->clear();
                        actor_->clear();
                        searchType_->clear();
                        keywordInput_->setText(keyword);
                        moodFilter_->setCurrentIndex(moodFilter_->findData(static_cast<int>(mood)));
                        contentFilter_->setCurrentIndex(contentFilter_->findData(category));
                        query_ = {};
                        keyword_ = keyword;
                        if (tabs_->currentIndex() == 1)
                            showSearchResults();
                        else
                            tabs_->setCurrentIndex(1);
                        keywordInput_->setFocus();
                    }, [this, banner](warm_mode::Scene scene) {
                        warmScene_ = scene;
                        const auto theme = warm_mode::theme(scene);
                        banner->setStyleSheet(scene == warm_mode::Scene::Neutral ? QString()
                            : QStringLiteral("QFrame#banner { background: %1; }").arg(theme.banner));
                        if (sceneSearch_) {
                            moodFilter_->setCurrentIndex(moodFilter_->findData(static_cast<int>(theme.mood)));
                            if (tabs_->currentIndex() == 1)
                                showSearchResults();
                        }
                    }, [this] {
                        tabs_->setCurrentIndex(2);
                    }, today_ ? QStringLiteral("打开今日推荐") : QStringLiteral("打开推荐"));
                }
                assistant_->show();
                assistant_->raise();
                assistant_->activateWindow();
            });
            if (!accountDatabasePath.isEmpty())
                bannerLayout->addWidget(account_ui::createAvatarButton(
                    accountDatabasePath, identity_->userId, identity_->username));
            auto* identityLabel = new QLabel(QStringLiteral("%1 · %2").arg(identity_->username,
                canManage() ? QStringLiteral("管理员") : QStringLiteral("普通用户")));
            identityLabel->setTextFormat(Qt::PlainText);
            identityLabel->setStyleSheet(QStringLiteral("color: white;"));
            bannerLayout->addWidget(identityLabel);
            auto* logout = new QPushButton(QStringLiteral("退出登录"));
            bannerLayout->addWidget(logout);
            connect(logout, &QPushButton::clicked, this, [this] {
                accounts_->logout();
                if (auto* session = qobject_cast<QDialog*>(parentWidget()))
                    session->accept();
                else
                    close();
            });
        }
        layout->addWidget(banner);
        add_->setObjectName(QStringLiteral("primary"));
        remove_->setObjectName(QStringLiteral("danger"));
        count_->setObjectName(QStringLiteral("resultCount"));
        order_->setObjectName(QStringLiteral("muted"));

        tabs_->setDrawBase(false);
        tabs_->addTab(QStringLiteral("媒体库"));
        tabs_->addTab(QStringLiteral("检索"));
        tabs_->addTab(today_ ? QStringLiteral("今日推荐") : QStringLiteral("推荐"));
        if (identity_)
            tabs_->addTab(QStringLiteral("资源仓库"));
        layout->addWidget(tabs_);

        auto* libraryControls = new QWidget;
        auto* toolbar = new QHBoxLayout(libraryControls);
        toolbar->setContentsMargins(0, 0, 0, 0);
        auto* filterLabel = new QLabel(QStringLiteral("文件类型"));
        filterLabel->setBuddy(filter_);
        toolbar->addWidget(filterLabel);
        toolbar->addWidget(filter_);
        toolbar->addWidget(add_);
        toolbar->addWidget(remove_);
        add_->setVisible(canManage());
        remove_->setVisible(canManage());
        toolbar->addStretch();

        auto* searchControls = new QWidget;
        auto* searchLayout = new QVBoxLayout(searchControls);
        searchLayout->setContentsMargins(0, 0, 0, 0);
        searchLayout->setSpacing(8);
        auto* keywordBar = new QHBoxLayout;
        auto* keywordLabel = new QLabel(QStringLiteral("文件名关键词"));
        keywordLabel->setBuddy(keywordInput_);
        keywordInput_->setPlaceholderText(QStringLiteral("输入片名的一部分，例如 海风"));
        keywordBar->addWidget(keywordLabel);
        keywordBar->addWidget(keywordInput_, 1);
        searchLayout->addLayout(keywordBar);

        auto* suggestions = new QCompleter(&keywordSuggestions_, keywordInput_);
        suggestions->setCaseSensitivity(Qt::CaseInsensitive);
        suggestions->setFilterMode(Qt::MatchContains);
        suggestions->setMaxVisibleItems(8);
        suggestions->popup()->setObjectName(QStringLiteral("keywordSuggestions"));
        keywordInput_->setCompleter(suggestions);

        auto* searchBar = new QHBoxLayout;
        searchLayout->addLayout(searchBar);
        auto* moodLabel = new QLabel(QStringLiteral("内容氛围"));
        moodLabel->setBuddy(moodFilter_);
        moodFilter_->setObjectName(QStringLiteral("searchMoodFilter"));
        using media_mood::Mood;
        for (auto mood : {Mood::Any, Mood::Relaxing, Mood::Resonant, Mood::Energizing, Mood::Nostalgic})
            moodFilter_->addItem(media_mood::label(mood), static_cast<int>(mood));
        auto* contentLabel = new QLabel(QStringLiteral("内容类别"));
        contentLabel->setBuddy(contentFilter_);
        contentFilter_->setObjectName(QStringLiteral("searchContentFilter"));
        contentFilter_->addItem(QStringLiteral("全部内容"), QString());
        for (const auto& category : {QStringLiteral("音乐"), QStringLiteral("电影"), QStringLiteral("电视剧")})
            contentFilter_->addItem(category, category);
        auto* moodBar = new QHBoxLayout;
        moodBar->addWidget(moodLabel);
        moodBar->addWidget(moodFilter_);
        moodBar->addWidget(contentLabel);
        moodBar->addWidget(contentFilter_);
        moodBar->addStretch();
        searchLayout->addLayout(moodBar);
        director_->setPlaceholderText(QStringLiteral("完整姓名"));
        actor_->setPlaceholderText(QStringLiteral("完整姓名"));
        searchType_->setPlaceholderText(QStringLiteral("例如 MP4"));
        auto* directorLabel = new QLabel(QStringLiteral("导演"));
        auto* actorLabel = new QLabel(QStringLiteral("演员"));
        auto* typeLabel = new QLabel(QStringLiteral("文件类型"));
        directorLabel->setBuddy(director_);
        actorLabel->setBuddy(actor_);
        typeLabel->setBuddy(searchType_);
        searchBar->addWidget(directorLabel);
        searchBar->addWidget(director_);
        searchBar->addWidget(actorLabel);
        searchBar->addWidget(actor_);
        searchBar->addWidget(typeLabel);
        searchBar->addWidget(searchType_);
        auto* searchButton = new QPushButton(QStringLiteral("查询"));
        searchButton->setObjectName(QStringLiteral("primary"));
        auto* clearButton = new QPushButton(QStringLiteral("清空条件"));
        searchBar->addWidget(searchButton);
        searchBar->addWidget(clearButton);
        auto* performance = new QPushButton(QStringLiteral("性能对照"));
        searchBar->addWidget(performance);
        connect(performance, &QPushButton::clicked, this, [this] { showPerformance(this); });

        auto* recommendationControls = new QWidget;
        auto* recommendationBar = new QHBoxLayout(recommendationControls);
        recommendationBar->setContentsMargins(0, 0, 0, 0);
        auto* categoryLabel = new QLabel(QStringLiteral("偏好类别"));
        categoryLabel->setBuddy(categoryPreference_);
        auto* preferenceTypeLabel = new QLabel(QStringLiteral("偏好文件类型"));
        preferenceTypeLabel->setBuddy(typePreference_);
        recommendationBar->addWidget(categoryLabel);
        recommendationBar->addWidget(categoryPreference_);
        recommendationBar->addWidget(preferenceTypeLabel);
        recommendationBar->addWidget(typePreference_);
        auto* recommend = new QPushButton(QStringLiteral("生成推荐"));
        recommend->setObjectName(QStringLiteral("primary"));
        recommendationBar->addWidget(recommend);
        recommendationBar->addStretch();
        connect(recommend, &QPushButton::clicked, this,
                [this] { refreshRecommendationPage(); });

        controls_->addWidget(libraryControls);
        controls_->addWidget(searchControls);
        controls_->addWidget(recommendationControls);
        controls_->addWidget(new QWidget); // Warehouse connection controls will be supplied by the client integration.
        controls_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        layout->addWidget(controls_);
        recommendationReason_->setTextFormat(Qt::PlainText);
        recommendationReason_->setWordWrap(true);
        recommendationReason_->setObjectName(QStringLiteral("muted"));
        recommendationReason_->hide();
        layout->addWidget(recommendationReason_);

        auto* summary = new QHBoxLayout;
        summary->addWidget(count_);
        summary->addStretch();
        summary->addWidget(order_);
        order_->setText(QStringLiteral("发行日期：从近到远"));
        layout->addLayout(summary);

        table_->setColumnCount(4);
        table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        table_->verticalHeader()->hide();
        table_->verticalHeader()->setDefaultSectionSize(36);
        table_->setShowGrid(false);
        table_->setAlternatingRowColors(true);
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        table_->setSelectionMode(QAbstractItemView::SingleSelection);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);

        auto* detailPane = new QFrame;
        detailPane->setObjectName(QStringLiteral("detailCard"));
        detailPane->setMinimumWidth(310);
        auto* detailLayout = new QVBoxLayout(detailPane);
        detailLayout->setContentsMargins(16, 14, 16, 14);
        auto* detailTitle = new QLabel(QStringLiteral("记录详情"));
        detailTitle->setObjectName(QStringLiteral("sectionTitle"));
        detailLayout->addWidget(detailTitle);
        auto* detailScroll = new QScrollArea;
        detailScroll->setObjectName(QStringLiteral("detailScroll"));
        detailScroll->setWidgetResizable(true);
        auto* detailBody = new QWidget;
        detailBody->setObjectName(QStringLiteral("detailBody"));
        detailScroll->setWidget(detailBody);
        detailLayout->addWidget(detailScroll);
        auto* form = new QFormLayout(detailBody);
        form->setContentsMargins(0, 4, 8, 0);
        form->setVerticalSpacing(9);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        const QStringList labels{QStringLiteral("编号"), QStringLiteral("文件名"),
                                 QStringLiteral("文件类型"), QStringLiteral("文件大小"),
                                 QStringLiteral("导演"), QStringLiteral("演员"),
                                 QStringLiteral("浏览次数"), QStringLiteral("发行日期"),
                                 QStringLiteral("所获奖项"), QStringLiteral("类别")};
        for (const auto& label : labels) {
            auto* value = new QLabel;
            value->setTextFormat(Qt::PlainText);
            value->setWordWrap(true);
            value->setTextInteractionFlags(Qt::TextSelectableByMouse);
            detailValues_.append(value);
            auto* fieldLabel = new QLabel(label);
            fieldLabel->setObjectName(QStringLiteral("fieldLabel"));
            form->addRow(fieldLabel, value);
        }

        auto* splitter = new QSplitter;
        splitter->setChildrenCollapsible(false);
        splitter->setHandleWidth(12);
        splitter->addWidget(table_);
        splitter->addWidget(detailPane);
        splitter->setStretchFactor(0, 3);
        splitter->setStretchFactor(1, 1);
        layout->addWidget(splitter, 1);

        todayTitle_->setObjectName(QStringLiteral("sectionTitle"));
        recommendationBar->insertWidget(0, todayTitle_);
        todayTitle_->hide();
        auto* todayBody = new QWidget;
        auto* todayLayout = new QVBoxLayout(todayBody);
        todayLayout->setContentsMargins(0, 0, 0, 0);
        todayGrid_ = new QGridLayout;
        todayGrid_->setSpacing(12);
        todayGrid_->setColumnStretch(0, 1);
        todayGrid_->setColumnStretch(1, 1);
        todayLayout->addLayout(todayGrid_);
        todayLayout->addWidget(todayStatus_);
        todayLayout->addStretch();
        todayPage_->setWidget(todayBody);
        todayPage_->setWidgetResizable(true);
        todayPage_->setFrameShape(QFrame::NoFrame);
        todayPage_->hide();
        layout->addWidget(todayPage_, 1);

        WarehousePage* warehousePage = nullptr;
        if (identity_) {
            auto* client = new WarehouseClient(this);
            warehousePage = new WarehousePage(*client, [this] { return library_.records(); });
            client->setParent(warehousePage);
            layout->addWidget(warehousePage, 1);
            warehousePage->hide();
        }
        trace_->setObjectName(QStringLiteral("tracePanel"));
        trace_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
        auto* traceLayout = new QVBoxLayout(trace_);
        traceLayout->setContentsMargins(12, 9, 12, 10);
        traceLayout->setSpacing(7);
        auto* traceHeading = new QLabel(QStringLiteral("检索过程"));
        traceHeading->setObjectName(QStringLiteral("sectionTitle"));
        traceLayout->addWidget(traceHeading);
        traceStatus_->setObjectName(QStringLiteral("muted"));
        traceStatus_->setTextFormat(Qt::PlainText);
        traceStatus_->setWordWrap(true);

        auto* steps = new QHBoxLayout;
        for (auto*& card : traceCards_) {
            card = new QLabel;
            card->setObjectName(QStringLiteral("traceStep"));
            card->setTextFormat(Qt::RichText);
            card->setWordWrap(true);
            steps->addWidget(card, 1);
        }
        traceLayout->addLayout(steps);
        traceLayout->addWidget(traceStatus_);
        trace_->hide();
        layout->addWidget(trace_);

        connect(tabs_, &QTabBar::currentChanged, this, [this, splitter, warehousePage](int index) {
            const bool showToday = index == 2 && today_ && identity_;
            const bool showWarehouse = index == 3;
            controls_->setVisible(!showWarehouse);
            splitter->setVisible(!showToday && !showWarehouse);
            count_->setVisible(!showToday && !showWarehouse);
            order_->setVisible(!showToday && !showWarehouse);
            todayTitle_->setVisible(showToday);
            todayPage_->setVisible(showToday);
            if (warehousePage) {
                if (warehousePage->isVisible() && !showWarehouse)
                    warehousePage->suspend();
                warehousePage->setVisible(showWarehouse);
            }
            if (!showWarehouse)
                controls_->setCurrentIndex(index);
            trace_->setVisible(index == 1);
            recommendationReason_->setVisible(index == 2 && !recommendationReason_->text().isEmpty());
            order_->setText(index == 2 ? QStringLiteral("按推荐顺序")
                                       : QStringLiteral("发行日期：从近到远"));
            if (index == 2) {
                const auto records = library_.records();
                refreshPreferences(records);
                refreshRecommendationPage();
            } else if (index == 1) {
                showSearchResults();
            } else if (index == 0) {
                refresh();
            }
        });
        const auto submitSearch = [this] {
            query_ = {director_->text(), actor_->text(), searchType_->text()};
            keyword_ = keywordInput_->text().trimmed();
            showSearchResults();
        };
        connect(searchButton, &QPushButton::clicked, this, submitSearch);
        connect(moodFilter_, &QComboBox::activated, this, [this, submitSearch] {
            sceneSearch_ = false;
            submitSearch();
        });
        connect(contentFilter_, &QComboBox::activated, this, submitSearch);
        for (auto* input : {keywordInput_, director_, actor_, searchType_})
            connect(input, &QLineEdit::returnPressed, this, submitSearch);
        connect(suggestions, qOverload<const QString&>(&QCompleter::activated), this,
                [this, submitSearch](const QString& fileName) {
                    keywordInput_->setText(fileName);
                    submitSearch();
                });
        connect(clearButton, &QPushButton::clicked, this, [this, submitSearch] {
            keywordInput_->clear();
            director_->clear();
            actor_->clear();
            searchType_->clear();
            sceneSearch_ = false;
            moodFilter_->setCurrentIndex(0);
            contentFilter_->setCurrentIndex(0);
            submitSearch();
            keywordInput_->setFocus();
        });

        connect(filter_, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
        connect(table_, &QTableWidget::cellClicked, this, [this](int row) {
            if (!accounts_ || !identity_ || row < 0 || row >= records_.size())
                return;
            recordMediaView(records_.at(row).id);
        });
        connect(table_, &QTableWidget::currentCellChanged, this,
                [this](int row) { showDetail(row); });
        connect(add_, &QPushButton::clicked, this, [this] { addRecord(); });
        connect(remove_, &QPushButton::clicked, this, [this] { removeRecord(); });
        refreshTypes();
    }

private:
    bool canManage() const
    {
        return !accounts_ || (identity_ && identity_->role == account_contract::UserRole::Admin);
    }

    // Collect a record and keep the form open if validation or saving fails.
    void addRecord()
    {
        if (!canManage())
            return;
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("新增媒体记录"));
        dialog.setMinimumWidth(420);
        auto* form = new QFormLayout(&dialog);
        form->setContentsMargins(22, 20, 22, 22);
        form->setVerticalSpacing(10);

        QLineEdit name, type, size(QStringLiteral("0")), director, actors;
        QLineEdit views(QStringLiteral("0")), date, awards, category;
        type.setPlaceholderText(QStringLiteral("例如 MP4"));
        date.setPlaceholderText(QStringLiteral("YYYY-MM-DD"));
        const QString listHint = QStringLiteral("多项用顿号「、」分隔");
        actors.setPlaceholderText(listHint);
        awards.setPlaceholderText(listHint);
        form->addRow(QStringLiteral("文件名"), &name);
        form->addRow(QStringLiteral("文件类型"), &type);
        form->addRow(QStringLiteral("文件大小（字节）"), &size);
        form->addRow(QStringLiteral("导演"), &director);
        form->addRow(QStringLiteral("演员"), &actors);
        form->addRow(QStringLiteral("浏览次数"), &views);
        form->addRow(QStringLiteral("发行日期"), &date);
        form->addRow(QStringLiteral("所获奖项"), &awards);
        form->addRow(QStringLiteral("类别"), &category);

        QDialogButtonBox buttons(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
        buttons.button(QDialogButtonBox::Save)->setText(QStringLiteral("保存"));
        buttons.button(QDialogButtonBox::Save)->setObjectName(QStringLiteral("primary"));
        buttons.button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
        form->addRow(&buttons);
        connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&] {
            MediaRecord record;
            record.fileName = name.text().trimmed();
            record.fileType = type.text().trimmed().toUpper();
            record.director = director.text().trimmed();
            record.actors = splitEntries(actors.text());
            record.awards = splitEntries(awards.text());
            record.category = category.text().trimmed();

            const QString dateText = date.text().trimmed();
            record.releaseDate = QDate::fromString(dateText, Qt::ISODate);
            if (record.fileName.isEmpty() || record.fileType.isEmpty() || record.fileType.contains('.')) {
                QMessageBox::warning(&dialog, QStringLiteral("检查输入"),
                                     QStringLiteral("请填写文件名和文件类型，类型无需带点。"));
                return;
            }
            const auto supportedTypes = MediaLibrary::supportedFileTypes();
            if (!supportedTypes.contains(record.fileType)) {
                QMessageBox::warning(&dialog, QStringLiteral("检查输入"),
                                     QStringLiteral("不支持的文件类型。支持类型：%1")
                                         .arg(supportedTypes.join(QStringLiteral("、"))));
                type.setFocus();
                type.selectAll();
                return;
            }
            if (!record.releaseDate.isValid() || record.releaseDate.toString(Qt::ISODate) != dateText) {
                QMessageBox::warning(&dialog, QStringLiteral("检查输入"),
                                     QStringLiteral("请输入有效的发行日期，格式为 YYYY-MM-DD。"));
                return;
            }
            if (!readCount(size.text(), record.fileSize) || !readCount(views.text(), record.viewCount)) {
                QMessageBox::warning(&dialog, QStringLiteral("检查输入"),
                                     QStringLiteral("文件大小和浏览次数须填写有效的非负整数。"));
                return;
            }

            QString id;
            if (accounts_) {
                const auto result = accounts_->addMedia(record);
                if (!result.persisted) {
                    QMessageBox::warning(&dialog, QStringLiteral("新增失败"), result.status.message);
                    return;
                }
                if (!reloadAfterAccountMutation(&dialog, result, QStringLiteral("新增"))) {
                    dialog.accept();
                    return;
                }
                id = result.mediaId;
            } else {
                const auto result = library_.addNew(record);
                id = record.id;
                if (!result.success) {
                    recoverAfterError(&dialog, QStringLiteral("新增失败"), result.error);
                    MediaRecord saved;
                    if (!library_.find(id, saved))
                        return;
                }
            }

            // If another file type was selected, switch to the new record's type.
            const QString activeType = filter_->currentData().toString();
            refreshTypes(activeType.isEmpty() ? QString() : record.fileType, id);
            dialog.accept();
        });
        name.setFocus();
        dialog.exec();
    }

    // Confirm the selected record and delete it by ID through the business layer.
    void removeRecord()
    {
        if (!canManage())
            return;
        const int row = table_->currentRow();
        if (row < 0 || row >= records_.size())
            return;
        const auto record = records_.at(row);
        QMessageBox confirmation(QMessageBox::Question, QStringLiteral("删除媒体记录"),
                                 QStringLiteral("确定删除「%1」的记录吗？").arg(record.fileName),
                                 QMessageBox::Yes | QMessageBox::Cancel, this);
        confirmation.setTextFormat(Qt::PlainText);
        confirmation.button(QMessageBox::Yes)->setText(QStringLiteral("删除"));
        confirmation.button(QMessageBox::Cancel)->setText(QStringLiteral("取消"));
        confirmation.setDefaultButton(QMessageBox::Cancel);
        if (confirmation.exec() != QMessageBox::Yes)
            return;

        if (accounts_) {
            const auto result = accounts_->removeMedia(record.id);
            if (!reloadAfterAccountMutation(this, result, QStringLiteral("删除")))
                return;
        } else {
            const auto result = library_.remove(record.id);
            if (!result.success) {
                recoverAfterError(this, QStringLiteral("删除失败"), result.error);
                return;
            }
        }
        refreshTypes(filter_->currentData().toString());
    }

    // Reload after a committed write, including writes that reported an index error.
    bool reloadAfterAccountMutation(QWidget* parent,
                                    const account_contract::MediaMutationResult& result,
                                    const QString& action)
    {
        if (!result.persisted) {
            QMessageBox::warning(parent, action + QStringLiteral("失败"), result.status.message);
            return false;
        }

        const auto reload = library_.load();
        if (!reload.success) {
            QMessageBox::warning(parent, action + QStringLiteral("已保存"),
                QStringLiteral("媒体库刷新失败，请重新打开程序。\n") + reload.error);
        } else if (!result.status.ok()) {
            QMessageBox::warning(parent, action + QStringLiteral("已保存，媒体库已刷新"),
                                 result.status.message);
        }
        return reload.success;
    }

    // Reload the current snapshot in case a write succeeded before indexing failed.
    void recoverAfterError(QWidget* parent, const QString& title, const QString& error)
    {
        const auto reload = library_.load();
        refreshTypes(filter_->currentData().toString());
        QMessageBox message(QMessageBox::Warning, title,
                            reload.success ? error : error + QStringLiteral("\n重新加载失败：") + reload.error,
                            QMessageBox::Ok, parent);
        message.setTextFormat(Qt::PlainText);
        message.exec();
    }

    // Restore the filter if the type still exists, or show all records.
    void refreshTypes(const QString& type = {}, const QString& selectedId = {})
    {
        const QSignalBlocker blockFilter(filter_);
        filter_->clear();
        filter_->addItem(QStringLiteral("全部类型"), QString());
        for (const auto& availableType : library_.fileTypes())
            filter_->addItem(availableType, availableType);
        const int index = filter_->findData(type);
        filter_->setCurrentIndex(index < 0 ? 0 : index);
        refresh(selectedId);

        QStringList names;
        for (const auto& record : library_.records())
            names.append(record.fileName);
        names.removeDuplicates();
        names.sort(Qt::CaseInsensitive);
        keywordSuggestions_.setStringList(names);
    }
    // Apply the selected file type and display the date order returned by the library.
    void refresh(const QString& selectedId = {})
    {
        populateTable(library_.records(filter_->currentData().toString()), selectedId);
        count_->setText(records_.isEmpty() ? QStringLiteral("暂无媒体记录")
                                          : QStringLiteral("共 %1 条").arg(records_.size()));
    }

    // Use the business layer's current index and keep the same display order as the library.
    void showSearchResults()
    {
        SearchTrace trace;
        const auto ids = library_.search(query_, trace);
        const QSet<QString> matched(ids.cbegin(), ids.cend());
        const auto mood = static_cast<media_mood::Mood>(moodFilter_->currentData().toInt());
        const QString category = contentFilter_->currentData().toString();
        QList<MediaRecord> results;
        for (const auto& record : library_.records()) {
            if (matched.contains(record.id) && media_mood::matches(record, mood) &&
                (category.isEmpty() || record.category == category) &&
                (keyword_.isEmpty() || record.fileName.contains(keyword_, Qt::CaseInsensitive)))
                results.append(record);
        }
        populateTable(results);
        count_->setText(QStringLiteral("命中 %1 条").arg(results.size()));

        QStringList filters;
        if (!keyword_.isEmpty())
            filters << QStringLiteral("文件名包含「%1」").arg(keyword_);
        if (mood != media_mood::Mood::Any)
            filters << media_mood::label(mood);
        if (!category.isEmpty())
            filters << category;
        traceStatus_->setText(filters.isEmpty()
            ? QStringLiteral("未填写条件，显示全部记录。")
            : QStringLiteral("%1 · 筛选后剩余 %2 条").arg(filters.join(QStringLiteral(" · "))).arg(results.size()));
        traceStatus_->setVisible(!filters.isEmpty() || trace.steps.isEmpty());
        for (std::size_t i = 0; i < traceCards_.size(); ++i) {
            auto* card = traceCards_[i];
            if (i >= static_cast<std::size_t>(trace.steps.size())) {
                card->hide();
                continue;
            }
            const auto& step = trace.steps.at(static_cast<qsizetype>(i));
            card->setText(QStringLiteral("<b>%1. %2</b><br>单项命中 %3 条 · <b>剩余 %4 条</b>")
                              .arg(i + 1).arg(fieldName(step.field))
                              .arg(step.postingCount).arg(step.remainingCount));
            card->show();
        }
    }

    // Refresh available preferences while preserving choices that still exist.
    void refreshPreferences(const QList<MediaRecord>& records)
    {
        QStringList categories;
        for (const auto& record : records) {
            const auto category = record.category.trimmed();
            if (!category.isEmpty() && !categories.contains(category))
                categories.append(category);
        }
        categories.sort();
        fillPreferences(categoryPreference_, categories);
        fillPreferences(typePreference_, library_.fileTypes());
    }

    // Populate a preference control with an empty option followed by available choices.
    void fillPreferences(QComboBox* combo, const QStringList& choices)
    {
        const QSignalBlocker block(combo);
        using recommendation_contract::PreferenceMode;
        const auto previous = combo->currentData();
        const auto previousMode = combo->currentData(Qt::UserRole + 1);
        combo->clear();
        const auto add = [combo](const QString& label, PreferenceMode mode, const QString& value = {}) {
            combo->addItem(label, value);
            combo->setItemData(combo->count() - 1, static_cast<int>(mode), Qt::UserRole + 1);
        };
        if (accounts_)
            add(QStringLiteral("自动"), PreferenceMode::Auto);
        add(QStringLiteral("不限"), PreferenceMode::Any);
        for (const auto& choice : choices)
            add(choice, PreferenceMode::Explicit, choice);
        const int index = previousMode.isValid() && previous.toString().isEmpty()
            ? combo->findData(previousMode, Qt::UserRole + 1) : combo->findData(previous);
        combo->setCurrentIndex(index < 0 ? 0 : index);
    }

    // Preserve the rank returned by the recommendation function in the shared table.
    void refreshRecommendationPage()
    {
        if (today_ && identity_)
            showTodayRecommendations();
        else
            showRecommendations(library_.records());
    }

    void showTodayRecommendations()
    {
        while (auto* item = todayGrid_->takeAt(0)) {
            if (auto* widget = item->widget()) {
                widget->hide();
                widget->deleteLater();
            }
            delete item;
        }

        const auto result = today_->recommendToday({identity_->userId,
            preferenceChoice(categoryPreference_), preferenceChoice(typePreference_)});
        todayTitle_->setText(result.dateKey.isEmpty()
            ? QStringLiteral("今日推荐") : QStringLiteral("今日推荐 · %1").arg(result.dateKey));
        recommendationReason_->setText(result.success ? result.reason : result.error);
        recommendationReason_->setVisible(!recommendationReason_->text().isEmpty());
        int position = 0;
        if (result.success) {
            for (const auto& id : result.mediaIds) {
                MediaRecord record;
                if (!library_.find(id, record))
                    continue;

                auto* card = new QFrame;
                card->setObjectName(QStringLiteral("recommendCard"));
                card->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
                auto* content = new QVBoxLayout(card);
                content->setContentsMargins(16, 14, 16, 14);
                auto* title = new QLabel(record.fileName);
                title->setObjectName(QStringLiteral("sectionTitle"));
                title->setTextFormat(Qt::PlainText);
                title->setWordWrap(true);
                content->addWidget(title);
                auto* metadata = new QLabel(QStringLiteral("%1 · %2 · %3 次浏览").arg(
                    displayText(record.category), record.fileType, QLocale().toString(record.viewCount)));
                metadata->setObjectName(QStringLiteral("muted"));
                metadata->setTextFormat(Qt::PlainText);
                metadata->setWordWrap(true);
                content->addWidget(metadata);
                content->addStretch();
                auto* details = new QPushButton(QStringLiteral("查看详情"));
                content->addWidget(details, 0, Qt::AlignLeft);
                connect(details, &QPushButton::clicked, card, [this, id] {
                    MediaRecord current;
                    if (!library_.find(id, current)) {
                        QMessageBox::information(this, QStringLiteral("记录详情"), QStringLiteral("该记录已移除。"));
                        return;
                    }
                    recordMediaView(id);
                    QMessageBox dialog(QMessageBox::Information, QStringLiteral("记录详情"), current.fileName,
                                       QMessageBox::Ok, this);
                    dialog.setTextFormat(Qt::PlainText);
                    dialog.setInformativeText(QStringLiteral("导演：%1\n演员：%2\n发行日期：%3\n所获奖项：%4")
                        .arg(displayText(current.director), displayText(current.actors.join(QStringLiteral("、"))),
                             displayDate(current.releaseDate), displayText(current.awards.join(QStringLiteral("、")))));
                    dialog.exec();
                });
                todayGrid_->addWidget(card, position / 2, position % 2);
                ++position;
            }
        }
        todayStatus_->setText(result.success && position == 0 ? QStringLiteral("暂无可推荐的媒体。") : QString());
        todayStatus_->setVisible(!todayStatus_->text().isEmpty());
    }

    static recommendation_contract::PreferenceSelection preferenceChoice(const QComboBox* combo)
    {
        return {static_cast<recommendation_contract::PreferenceMode>(combo->currentData(Qt::UserRole + 1).toInt()),
                combo->currentData().toString()};
    }

    void recordMediaView(const QString& id)
    {
        if (!accounts_ || !identity_)
            return;
        const auto status = accounts_->recordMediaView(identity_->userId, id);
        if (!status.ok())
            QMessageBox::warning(this, QStringLiteral("保存浏览记录失败"), status.message);
    }

    void showRecommendations(const QList<MediaRecord>& records)
    {
        QStringList ids;
        QString reason;
        if (accounts_) {
            using namespace recommendation_contract;
            const auto result = identity_ && recommendations_
                ? recommendations_->recommend({identity_->userId, preferenceChoice(categoryPreference_), preferenceChoice(typePreference_)})
                : RecommendationResult{false, QStringLiteral("个性化推荐服务暂不可用。"), {}, {}};
            ids = result.success ? result.mediaIds : QStringList();
            reason = result.success ? result.reason : result.error;
        } else {
            RecommendationOptions options;
            options.preferredCategory = categoryPreference_->currentData().toString();
            options.preferredFileType = typePreference_->currentData().toString();
            ids = recommendMedia(records, options);
        }
        recommendationReason_->setText(reason);
        recommendationReason_->setVisible(!reason.isEmpty());
        QList<MediaRecord> results;
        for (const auto& id : ids) {
            MediaRecord record;
            if (library_.find(id, record))
                results.append(record);
        }
        populateTable(results, {}, true);
        count_->setText(QStringLiteral("推荐 %1 条").arg(results.size()));
    }

    // Update the shared table and details from the current page.
    void populateTable(const QList<MediaRecord>& records, const QString& selectedId = {},
                       bool recommended = false)
    {
        const QSignalBlocker blockSelection(table_);
        auto* header = table_->horizontalHeader();
        QList<QHeaderView::ResizeMode> resizeModes;
        for (int column = 0; column < table_->columnCount(); ++column)
            resizeModes.append(header->sectionResizeMode(column));
        const bool updatesEnabled = table_->updatesEnabled();
        const auto restoreLayout = qScopeGuard([&] {
            // Restoring automatic modes schedules one resize after the batch.
            for (int column = 0; column < resizeModes.size(); ++column)
                header->setSectionResizeMode(column, resizeModes.at(column));
            table_->setUpdatesEnabled(updatesEnabled);
        });
        table_->setUpdatesEnabled(false);
        // Blocking widget signals or painting alone does not stop model dataChanged
        // from triggering ResizeToContents scans for every replaced cell.
        header->setSectionResizeMode(QHeaderView::Fixed);
        table_->clearSelection();
        records_ = records;
        table_->setHorizontalHeaderLabels({QStringLiteral("文件名"), QStringLiteral("类型"),
            recommended ? QStringLiteral("类别") : QStringLiteral("导演"),
            recommended ? QStringLiteral("浏览次数") : QStringLiteral("发行日期")});
        table_->setRowCount(static_cast<int>(records_.size()));
        int selectedRow = 0;
        for (int row = 0; row < table_->rowCount(); ++row) {
            const auto& record = records_.at(row);
            if (record.id == selectedId)
                selectedRow = row;
            const QStringList values{record.fileName, record.fileType,
                recommended ? record.category : record.director,
                recommended ? QLocale().toString(record.viewCount) : displayDate(record.releaseDate)};
            for (int column = 0; column < values.size(); ++column)
                table_->setItem(row, column, new QTableWidgetItem(displayText(values.at(column))));
        }

        if (records_.isEmpty()) {
            showDetail(-1);
            return;
        }
        table_->setCurrentCell(selectedRow, 0);
        showDetail(selectedRow);
    }

    // Update the details from the record associated with the current table row.
    void showDetail(int row)
    {
        const bool validRow = row >= 0 && row < records_.size();
        remove_->setEnabled(validRow && canManage());
        if (!validRow) {
            for (auto* value : detailValues_)
                value->setText(displayText({}));
            return;
        }

        const auto& record = records_.at(row);
        const QLocale locale;
        const QStringList values{record.id, record.fileName, record.fileType,
                                 record.fileType.isEmpty() ? QStringLiteral("未知") : QStringLiteral("%1 字节").arg(locale.toString(record.fileSize)),
                                 record.director, record.actors.join(QStringLiteral("、")),
                                 locale.toString(record.viewCount), displayDate(record.releaseDate),
                                 record.awards.join(QStringLiteral("、")), record.category};
        for (qsizetype i = 0; i < values.size(); ++i)
            detailValues_.at(i)->setText(displayText(values.at(i)));
    }

    account_contract::AccountHistoryService* accounts_;
    recommendation_contract::PersonalRecommendationService* recommendations_;
    recommendation_contract::TodayRecommendationService* today_;
    const std::optional<account_contract::UserIdentity> identity_;
    QScrollArea* todayPage_ = new QScrollArea;
    QLabel* todayTitle_ = new QLabel;
    QLabel* todayStatus_ = new QLabel;
    QGridLayout* todayGrid_ = nullptr;
    QLabel* recommendationReason_ = new QLabel;
    MediaLibrary& library_;            // Loaded business service, owned by main.
    QList<MediaRecord> records_;       // Visible records in table row order.
    QList<QLabel*> detailValues_;      // Detail value labels in form order.
    SearchQuery query_; // Last submitted combination of search fields.
    AssistantDialog* assistant_ = nullptr;
    bool sceneSearch_ = false;
    warm_mode::Scene warmScene_ = warm_mode::Scene::Neutral;
    QTabBar* tabs_ = new QTabBar; // Switch between browsing and searching.
    QStackedWidget* controls_ = new QStackedWidget; // Controls for the selected tab.
    QLineEdit* director_ = new QLineEdit; // Director full-name query.
    QLineEdit* actor_ = new QLineEdit; // Actor full-name query.
    QString keyword_; // Last submitted file-name keyword.
    QComboBox* moodFilter_ = new QComboBox;
    QComboBox* contentFilter_ = new QComboBox;
    QStringListModel keywordSuggestions_; // Current library file names for completion.
    QLineEdit* keywordInput_ = new QLineEdit; // Partial file-name query.
    QLineEdit* searchType_ = new QLineEdit; // File extension query.
    QComboBox* categoryPreference_ = new QComboBox; // Preferred category.
    QComboBox* typePreference_ = new QComboBox; // Preferred extension.
    QLabel* order_ = new QLabel; // Current table ordering.
    QFrame* trace_ = new QFrame; // Search trace panel.
    QLabel* traceStatus_ = new QLabel; // Explanation when there are no query conditions.
    std::array<QLabel*, 3> traceCards_{}; // One card for each possible index step.
    QComboBox* filter_ = new QComboBox; // File type filter with an empty value for all types.
    QPushButton* add_ = new QPushButton(QStringLiteral("新增")); // Open the record form.
    QPushButton* remove_ = new QPushButton(QStringLiteral("删除")); // Delete the selected row.
    QLabel* count_ = new QLabel;       // Visible record count or empty state.
    QTableWidget* table_ = new QTableWidget; // Read-only list of filtered records.
};

} // namespace

// Run the account flow with the application-owned services.
int runAccountFrontend(QApplication& app, MediaLibrary& library,
                       account_contract::AccountHistoryService& accounts,
                       recommendation_contract::PersonalRecommendationService& recommendations,
                       const QString& databasePath,
                       recommendation_contract::TodayRecommendationService* today)
{
    const auto initialized = accounts.initializeDatabase(databasePath);
    if (!initialized.ok()) {
        QMessageBox::critical(nullptr, QStringLiteral("账户数据库初始化失败"), initialized.message);
        return 1;
    }

    // Closing the library on logout must not quit before the next login dialog opens.
    const bool quitOnClose = app.quitOnLastWindowClosed();
    app.setQuitOnLastWindowClosed(false);
    while (account_ui::signIn(accounts) && accounts.currentUser()) {
        QDialog session;
        session.setWindowTitle(QStringLiteral("多媒体文件管理及检索系统"));
        session.resize(1120, 710);
        auto* layout = new QVBoxLayout(&session);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* window = new LibraryWindow(library, &accounts, &recommendations, today, databasePath);
        layout->addWidget(window);
        session.exec();
        const bool loggedOut = !accounts.currentUser();
        accounts.logout();
        if (!loggedOut)
            break;
    }
    app.setQuitOnLastWindowClosed(quitOnClose);
    return 0;
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setStyle(QStringLiteral("Fusion"));
    app.setFont(QFont(QStringLiteral("Microsoft YaHei UI")));
    app.setStyleSheet(libraryStyle());
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("多媒体文件管理及检索系统"));
    parser.addHelpOption();
    const QCommandLineOption csvOption(QStringLiteral("csv"), QStringLiteral("媒体 CSV 文件路径"),
                                       QStringLiteral("path"));
    parser.addOption(csvOption);
    parser.process(app);

    const QDir appDir(QCoreApplication::applicationDirPath());
    const bool useDefaultCsv = !parser.isSet(csvOption);
    const QString csvPath = useDefaultCsv ? appDir.filePath(QStringLiteral("media_records.csv"))
        : QFileInfo(parser.value(csvOption)).absoluteFilePath();

    // Initialize the writable demo file once. Subsequent builds only copy the seed.
    if (useDefaultCsv && !QFile::exists(csvPath)) {
        QFile seed(appDir.filePath(QStringLiteral("media_records_sample.csv")));
        if (!seed.copy(csvPath) && !QFile::exists(csvPath)) {
            QMessageBox::critical(nullptr, QStringLiteral("无法初始化媒体库"), seed.errorString());
            return 1;
        }
    }

    MediaLibrary library(csvPath, QString());
    const auto result = library.load();
    if (!result.success) {
        QMessageBox::critical(nullptr, QStringLiteral("媒体库加载失败"), result.error);
        return 1;
    }

    AccountHistoryServiceImpl accounts(library);
    recommendation_contract::PersonalRecommendationServiceImpl recommendations(
        accounts, [&library] { return library.records(); });
    const QString databasePath = AccountHistoryServiceImpl::defaultDatabasePath();
    if (databasePath.isEmpty() || !QDir().mkpath(QFileInfo(databasePath).absolutePath())) {
        QMessageBox::critical(nullptr, QStringLiteral("账户数据库初始化失败"),
                              QStringLiteral("无法创建应用数据目录。"));
        return 1;
    }
    recommendation_contract::TodayRecommendationServiceImpl today(
        recommendations, [&library] { return library.records(); });
    return runAccountFrontend(app, library, accounts, recommendations, databasePath, &today);
}
