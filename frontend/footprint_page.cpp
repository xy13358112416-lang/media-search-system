// >>> DEFENSE-NOTE | 文件导读｜个人足迹页面
// 阅读路线：选日期 → 请求个人报告 → 统计/日期/类别显示 → 比较期与清历史
// 联调关系：算法buildFootprint提供统计，服务端确认本人身份；页面不根据界面列表自行伪造总数。
// 标有“答辩导读”的内容仅为讲解注释；原有条件、接口和算法不变。
// <<< DEFENSE-NOTE | 本段讲解结束
#include "footprint_page.h"

#include <QComboBox>
#include <QDateEdit>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <algorithm>
#include <QTableWidget>
#include <QTabWidget>

using namespace care_contract;

namespace {

QDate periodStart(const QDate& date, int mode)
{
    if (mode == 0)
        return date.addDays(1 - date.dayOfWeek());
    if (mode == 1)
        return QDate(date.year(), date.month(), 1);
    return QDate(date.year(), 1, 1);
}

QDate nextPeriod(QDate begin, int mode, int steps)
{
    if (mode == 0)
        return begin.addDays(steps * 7);
    if (mode == 1)
        return begin.addMonths(steps);
    return begin.addYears(steps);
}

QString statistics(const PeriodFootprint& period)
{
    return QStringLiteral("浏览 %1 次 · 搜索 %2 次 · 作品 %3 部 · 活跃 %4 天")
        .arg(period.views).arg(period.searches).arg(period.uniqueWorks).arg(period.activeDays);
}

void fill(QTableWidget* table, const QList<QStringList>& rows)
{
    table->setRowCount(rows.size());
    for (int row = 0; row < rows.size(); ++row)
        for (int col = 0; col < rows.at(row).size(); ++col)
            table->setItem(row, col, new QTableWidgetItem(rows.at(row).at(col)));
}

class FootprintPage final : public CarePage {
public:
    FootprintPage(CareService& service, std::function<bool()> signedIn)
        : CarePage(QStringLiteral("个人媒体足迹")), service_(service), signedIn_(std::move(signedIn))
    {
        const auto today = care_ui::localTime(QDateTime::currentSecsSinceEpoch()).date();
        auto* bar = new QHBoxLayout;
        mode_->addItems({QStringLiteral("自然周"), QStringLiteral("自然月"), QStringLiteral("自然年"), QStringLiteral("自选区间")});
        mode_->setAccessibleName(QStringLiteral("统计区间类型"));
        mode_->setObjectName(QStringLiteral("footprintPeriod"));
        from_->setDate(today.addDays(-6));
        to_->setDate(today);
        from_->setAccessibleName(QStringLiteral("开始日期"));
        to_->setAccessibleName(QStringLiteral("结束日期"));
        for (auto* date : {from_, to_}) {
            date->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
            date->setCalendarPopup(true);
            date->setDateRange(QDate(1970, 1, 2), today);
        }
        bar->addWidget(mode_);
        auto* dates = new QHBoxLayout(dates_);
        dates->setContentsMargins(0, 0, 0, 0);
        dates->addWidget(from_);
        dates->addWidget(new QLabel(QStringLiteral("至")));
        dates->addWidget(to_);
        dates_->hide();
        bar->addWidget(dates_);
        presetHint_->setObjectName(QStringLiteral("muted"));
        bar->addWidget(presetHint_);
        bar->addStretch();
        bar->addWidget(fetch_);
        layout->addLayout(bar);
        counts_->setObjectName(QStringLiteral("sectionTitle"));
        counts_->setAccessibleName(QStringLiteral("足迹统计"));
        counts_->setObjectName(QStringLiteral("footprintCounts"));
        layout->addWidget(counts_);
        note_->setTextFormat(Qt::PlainText);
        note_->setWordWrap(true);
        layout->addWidget(note_);
        auto* tabs = new QTabWidget;
        tabs->addTab(days_, QStringLiteral("每日活动"));
        tabs->addTab(categories_, QStringLiteral("类别分布"));
        tabs->addTab(people_, QStringLiteral("人物分布"));
        layout->addWidget(tabs, 1);
        setup(days_, {QStringLiteral("北京时间"), QStringLiteral("浏览次数"), QStringLiteral("搜索次数")});
        setup(categories_, {QStringLiteral("类别"), QStringLiteral("浏览次数"), QStringLiteral("相对分布")});
        setup(people_, {QStringLiteral("人物"), QStringLiteral("角色"), QStringLiteral("浏览次数")});
        auto* actions = new QHBoxLayout;
        actions->addStretch();
        clearHistory_->setObjectName(QStringLiteral("danger"));
        actions->addWidget(clearHistory_);
        layout->addLayout(actions);
        connect(clearHistory_, &QPushButton::clicked, this, [this] { clearHistory(); });
        connect(mode_, &QComboBox::currentIndexChanged, this, [this] { activate(); });
        connect(fetch_, &QPushButton::clicked, this, [this] { activate(); });
    }

    // >>> DEFENSE-NOTE | activate = 加载足迹与比较期
    // 请求由北京时间自然日控件转换的时间范围，清理旧图表；返回报告包含历史覆盖与模拟标记。
    // <<< DEFENSE-NOTE | 本段讲解结束
    void activate() override
    {
        active_ = true;
        clear();
        invalidate(0);
        const bool connected = !signedIn_ || signedIn_();
        mode_->setEnabled(connected);
        fetch_->setEnabled(connected);
        clearHistory_->setEnabled(connected && !clearing_);
        if (!connected) {
            dates_->hide();
            presetHint_->hide();
            status->setText(QStringLiteral("请先到「资源仓库」登录服务器账号。"));
            return;
        }
        const auto today = care_ui::localTime(QDateTime::currentSecsSinceEpoch()).date();
        const auto mode = mode_->currentIndex();
        const bool custom = mode == 3;
        dates_->setVisible(custom);
        presetHint_->setVisible(!custom);
        auto start = custom ? from_->date() : periodStart(today, mode);
        auto end = custom ? to_->date().addDays(1) : nextPeriod(start, mode, 1);
        if (start >= end) {
            status->setText(QStringLiteral("开始日期应早于或等于结束日期。"));
            return;
        }
        FootprintRequest request{{care_ui::midnight(start), care_ui::midnight(end)}, std::nullopt};
        if (!custom)
            request.comparison = TimeRange{care_ui::midnight(nextPeriod(start, mode, -1)), request.range.beginUtc};
        status->setText(QStringLiteral("正在加载足迹…"));
        service_.getFootprint(request, latest(0, [this](Result<FootprintReport> result) {
            if (!result.status.ok() || !result.value) {
                reportError(result);
                return;
            }
            const auto& data = *result.value;
            const auto& current = data.current;
            counts_->setText(statistics(current));
            status->setText(QStringLiteral("%1 至 %2 · 北京时间%3")
                .arg(care_ui::localTime(current.range.beginUtc).date().toString(Qt::ISODate),
                     care_ui::localTime(current.range.endUtc - 1).date().toString(Qt::ISODate),
                     data.containsSyntheticData ? QStringLiteral(" · 含模拟数据") : QString()));
            QStringList notes;
            notes << (current.coverageStartUtc ? QStringLiteral("记录起于 %1").arg(care_ui::timestamp(*current.coverageStartUtc))
                                               : QStringLiteral("所选时段暂无浏览或搜索记录"));
            if (data.comparison)
                notes << QStringLiteral("前一自然区间 %1 至 %2 · %3")
                    .arg(care_ui::localTime(data.comparison->range.beginUtc).date().toString(Qt::ISODate),
                         care_ui::localTime(data.comparison->range.endUtc - 1).date().toString(Qt::ISODate),
                         statistics(*data.comparison));
            if (!data.message.isEmpty())
                notes << data.message;
            note_->setText(notes.join(QStringLiteral("\n")));
            QList<QStringList> rows;
            for (const auto& day : current.days)
                rows.append({day.localDate, QString::number(day.views), QString::number(day.searches)});
            fill(days_, rows);
            rows.clear();
            for (const auto& item : current.categories)
                rows.append({item.label, QString::number(item.count)});
            fill(categories_, rows);
            qint64 largest = 0;
            for (const auto& item : current.categories)
                largest = std::max(largest, item.count);
            for (int row = 0; row < current.categories.size(); ++row) {
                const auto& item = current.categories.at(row);
                auto* bar = new QProgressBar;
                bar->setAccessibleName(item.label);
                bar->setAccessibleDescription(QString::number(item.count));
                bar->setRange(0, 1000);
                bar->setTextVisible(false);
                bar->setValue(largest > 0 ? qRound(1000.0 * item.count / largest) : 0);
                categories_->setCellWidget(row, 2, bar);
            }
            rows.clear();
            for (const auto& item : current.people)
                rows.append({item.name, care_ui::roleLabel(item.role), QString::number(item.count)});
            fill(people_, rows);
        }));
    }

    void deactivate() override
    {
        active_ = false;
        CarePage::deactivate();
        clear();
        status->clear();
    }

private:
    // >>> DEFENSE-NOTE | clearHistory = 主动清除本人历史
    // 由服务端执行清理，页面清旧报告并显示结果；不能将清空界面误当数据库删除成功。
    // <<< DEFENSE-NOTE | 本段讲解结束
    void clearHistory()
    {
        if (QMessageBox::question(this, QStringLiteral("清理浏览和搜索历史"),
                QStringLiteral("清理后无法恢复。已关注的人物和「不感兴趣」记录会保留。确定清理？"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes ||
                (signedIn_ && !signedIn_()))
            return;
        clearing_ = true;
        clearHistory_->setEnabled(false);
        invalidate(0);
        status->setText(QStringLiteral("正在清理历史…"));
        service_.clearHistory([page = QPointer<FootprintPage>(this)](Status result) {
            if (!page)
                return;
            page->clearing_ = false;
            if (!page->active_ || (page->signedIn_ && !page->signedIn_()))
                return;
            page->clearHistory_->setEnabled(true);
            if (result.ok())
                page->activate();
            else
                page->reportError(result);
        });
    }

    void setup(QTableWidget* table, const QStringList& headings)
    {
        table->setColumnCount(headings.size());
        table->setHorizontalHeaderLabels(headings);
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        table->verticalHeader()->hide();
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setAlternatingRowColors(true);
    }

    void clear()
    {
        counts_->clear();
        note_->clear();
        for (auto* table : {days_, categories_, people_})
            table->setRowCount(0);
    }

    CareService& service_;
    std::function<bool()> signedIn_;
    bool active_ = false;
    bool clearing_ = false;
    QComboBox* mode_ = new QComboBox;
    QPushButton* fetch_ = new QPushButton(QStringLiteral("查看足迹"));
    QPushButton* clearHistory_ = new QPushButton(QStringLiteral("清理历史"));
    QWidget* dates_ = new QWidget;
    QLabel* presetHint_ = new QLabel(QStringLiteral("手动选择日期请切换到「自选区间」"));
    QDateEdit* from_ = new QDateEdit;
    QDateEdit* to_ = new QDateEdit;
    QLabel* counts_ = new QLabel;
    QLabel* note_ = new QLabel;
    QTableWidget* days_ = new QTableWidget;
    QTableWidget* categories_ = new QTableWidget;
    QTableWidget* people_ = new QTableWidget;
};

} // namespace

CarePage* createFootprintPage(CareService& service, std::function<bool()> signedIn)
{
    return new FootprintPage(service, std::move(signedIn));
}
