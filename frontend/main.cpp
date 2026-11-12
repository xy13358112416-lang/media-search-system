#include "medialibrary.h"
#include "mediarecommendation.h"
#include "library_style.h"

#include <QApplication>
#include <QCheckBox>
#include <QCommandLineParser>
#include <QCompleter>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
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

// Keep the password masked without telling the Windows input context to disable IME.
QLineEdit* addPasswordField(QVBoxLayout* layout, const QString& placeholder)
{
    auto* input = new QLineEdit;
    input->setPlaceholderText(placeholder);
    input->setAccessibleName(QStringLiteral("系统口令"));
    auto* visibility = new QCheckBox(QStringLiteral("显示口令"));
    const auto setVisible = [input](bool visible) {
        input->setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password);
        input->setInputMethodHints(Qt::ImhSensitiveData | Qt::ImhNoAutoUppercase);
    };
    setVisible(false);
    QObject::connect(visibility, &QCheckBox::toggled, input, setVisible);
    layout->addWidget(input);
    layout->addWidget(visibility);
    return input;
}

QString setupDemoPassword()
{
    QDialog dialog;
    dialog.setWindowTitle(QStringLiteral("设置演示口令"));
    dialog.setMinimumWidth(390);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(28, 26, 28, 28);
    layout->setSpacing(14);
    auto* title = new QLabel(QStringLiteral("设置演示口令"));
    title->setObjectName(QStringLiteral("dialogTitle"));
    layout->addWidget(title);
    auto* input = addPasswordField(layout, QStringLiteral("请输入本次演示使用的口令"));
    auto* error = new QLabel(QStringLiteral("口令不能为空，请重新输入。"));
    error->setStyleSheet(QStringLiteral("color: #b42318;"));
    error->hide();
    layout->addWidget(error);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("继续"));
    buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("primary"));
    buttons->button(QDialogButtonBox::Ok)->setDefault(true);
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(input, &QLineEdit::textEdited, error, &QWidget::hide);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (input->text().trimmed().isEmpty()) {
            error->show();
            input->setFocus();
            return;
        }
        dialog.accept();
    });
    input->setFocus();
    return dialog.exec() == QDialog::Accepted ? input->text() : QString();
}

// Verify the entered password before opening the media library.
bool showLogin(const MediaLibrary& library)
{
    QDialog dialog;
    dialog.setWindowTitle(QStringLiteral("媒体库登录"));
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(28, 26, 28, 28);
    layout->setSpacing(14);
    auto* title = new QLabel(QStringLiteral("登录媒体库"));
    title->setObjectName(QStringLiteral("dialogTitle"));
    auto* error = new QLabel;
    error->setStyleSheet(QStringLiteral("color: #b42318;"));
    error->hide();
    auto* login = new QPushButton(QStringLiteral("登录"));
    login->setDefault(true);
    login->setObjectName(QStringLiteral("primary"));
    layout->addWidget(title);
    auto* password = addPasswordField(layout, QStringLiteral("请输入系统口令"));
    layout->addWidget(error);
    layout->addWidget(login);
    dialog.setMinimumWidth(390);
    password->setFocus();

    QObject::connect(login, &QPushButton::clicked, &dialog, [&] {
        if (library.authenticate(password->text())) {
            dialog.accept();
            return;
        }
        error->setText(QStringLiteral("口令错误，请重新输入。"));
        error->show();
        password->selectAll();
        password->setFocus();
    });
    QObject::connect(password, &QLineEdit::textEdited, error, &QWidget::hide);
    return dialog.exec() == QDialog::Accepted;
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
    explicit LibraryWindow(MediaLibrary& library) : library_(library)
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
        layout->addWidget(banner);
        add_->setObjectName(QStringLiteral("primary"));
        remove_->setObjectName(QStringLiteral("danger"));
        count_->setObjectName(QStringLiteral("resultCount"));
        order_->setObjectName(QStringLiteral("muted"));

        tabs_->setDrawBase(false);
        tabs_->addTab(QStringLiteral("媒体库"));
        tabs_->addTab(QStringLiteral("检索"));
        tabs_->addTab(QStringLiteral("推荐"));
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
                [this] { showRecommendations(library_.records()); });

        controls_->addWidget(libraryControls);
        controls_->addWidget(searchControls);
        controls_->addWidget(recommendationControls);
        controls_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        layout->addWidget(controls_);

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

        connect(tabs_, &QTabBar::currentChanged, this, [this](int index) {
            controls_->setCurrentIndex(index);
            trace_->setVisible(index == 1);
            order_->setText(index == 2 ? QStringLiteral("按推荐顺序")
                                       : QStringLiteral("发行日期：从近到远"));
            if (index == 2) {
                const auto records = library_.records();
                refreshPreferences(records);
                showRecommendations(records);
            } else if (index == 1) {
                showSearchResults();
            } else {
                refresh();
            }
        });
        const auto submitSearch = [this] {
            query_ = {director_->text(), actor_->text(), searchType_->text()};
            keyword_ = keywordInput_->text().trimmed();
            showSearchResults();
        };
        connect(searchButton, &QPushButton::clicked, this, submitSearch);
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
            submitSearch();
            keywordInput_->setFocus();
        });

        connect(filter_, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
        connect(table_, &QTableWidget::currentCellChanged, this,
                [this](int row) { showDetail(row); });
        connect(add_, &QPushButton::clicked, this, [this] { addRecord(); });
        connect(remove_, &QPushButton::clicked, this, [this] { removeRecord(); });
        refreshTypes();
    }

private:
    // Collect a record and keep the form open if validation or saving fails.
    void addRecord()
    {
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

            const auto result = library_.addNew(record);
            const QString id = record.id;
            if (!result.success) {
                recoverAfterError(&dialog, QStringLiteral("新增失败"), result.error);
                MediaRecord saved;
                if (!library_.find(id, saved))
                    return;
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

        const auto result = library_.remove(record.id);
        if (!result.success) {
            recoverAfterError(this, QStringLiteral("删除失败"), result.error);
            return;
        }
        refreshTypes(filter_->currentData().toString());
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
        QList<MediaRecord> results;
        for (const auto& record : library_.records()) {
            if (matched.contains(record.id) &&
                (keyword_.isEmpty() || record.fileName.contains(keyword_, Qt::CaseInsensitive)))
                results.append(record);
        }
        populateTable(results);
        count_->setText(QStringLiteral("命中 %1 条").arg(results.size()));

        const bool hasKeyword = !keyword_.isEmpty();
        traceStatus_->setText(hasKeyword
            ? QStringLiteral("文件名包含「%1」 · 筛选后剩余 %2 条").arg(keyword_).arg(results.size())
            : QStringLiteral("未填写条件，显示全部记录。"));
        traceStatus_->setVisible(hasKeyword || trace.steps.isEmpty());
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
        for (const auto& record : records)
            if (!record.category.isEmpty() && !categories.contains(record.category))
                categories.append(record.category);
        categories.sort();
        fillPreferences(categoryPreference_, categories);
        fillPreferences(typePreference_, library_.fileTypes());
    }

    // Populate a preference control with an empty option followed by available choices.
    static void fillPreferences(QComboBox* combo, const QStringList& choices)
    {
        const QSignalBlocker block(combo);
        const auto previous = combo->currentData();
        combo->clear();
        combo->addItem(QStringLiteral("不限"), QString());
        for (const auto& choice : choices)
            combo->addItem(choice, choice);
        const int index = combo->findData(previous);
        combo->setCurrentIndex(index < 0 ? 0 : index);
    }

    // Preserve the rank returned by the recommendation function in the shared table.
    void showRecommendations(const QList<MediaRecord>& records)
    {
        RecommendationOptions options;
        options.preferredCategory = categoryPreference_->currentData().toString();
        options.preferredFileType = typePreference_->currentData().toString();
        const auto ids = recommendMedia(records, options);
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
        remove_->setEnabled(row >= 0 && row < records_.size());
        if (!remove_->isEnabled()) {
            for (auto* value : detailValues_)
                value->setText(displayText({}));
            return;
        }

        const auto& record = records_.at(row);
        const QLocale locale;
        const QStringList values{record.id, record.fileName, record.fileType,
                                 QStringLiteral("%1 字节").arg(locale.toString(record.fileSize)),
                                 record.director, record.actors.join(QStringLiteral("、")),
                                 locale.toString(record.viewCount), displayDate(record.releaseDate),
                                 record.awards.join(QStringLiteral("、")), record.category};
        for (qsizetype i = 0; i < values.size(); ++i)
            detailValues_.at(i)->setText(displayText(values.at(i)));
    }

    MediaLibrary& library_;            // Loaded business service, owned by main.
    QList<MediaRecord> records_;       // Visible records in table row order.
    QList<QLabel*> detailValues_;      // Detail value labels in form order.
    SearchQuery query_; // Last submitted combination of search fields.
    QTabBar* tabs_ = new QTabBar; // Switch between browsing and searching.
    QStackedWidget* controls_ = new QStackedWidget; // Controls for the selected tab.
    QLineEdit* director_ = new QLineEdit; // Director full-name query.
    QLineEdit* actor_ = new QLineEdit; // Actor full-name query.
    QString keyword_; // Last submitted file-name keyword.
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

    QString password = qEnvironmentVariable("MEDIA_LIBRARY_PASSWORD");
    if (password.isEmpty())
        password = setupDemoPassword();
    if (password.isEmpty())
        return 0;

    const QDir appDir(QCoreApplication::applicationDirPath());
    const bool useDefaultCsv = !parser.isSet(csvOption);
    const QString csvPath = useDefaultCsv ? appDir.filePath(QStringLiteral("media_records.csv"))
                                          : parser.value(csvOption);
    MediaLibrary library(csvPath, password);
    if (!showLogin(library))
        return 0;

    // Initialize the writable demo file once. Subsequent builds only copy the seed.
    if (useDefaultCsv && !QFile::exists(csvPath)) {
        QFile seed(appDir.filePath(QStringLiteral("media_records_sample.csv")));
        if (!seed.copy(csvPath) && !QFile::exists(csvPath)) {
            QMessageBox::critical(nullptr, QStringLiteral("无法初始化媒体库"), seed.errorString());
            return 1;
        }
    }

    const auto result = library.load();
    if (!result.success) {
        QMessageBox::critical(nullptr, QStringLiteral("媒体库加载失败"), result.error);
        return 1;
    }

    LibraryWindow window(library);
    window.show();
    return app.exec();
}
