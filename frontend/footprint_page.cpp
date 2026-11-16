#include "footprint_page.h"

#include <QComboBox>
#include <QDateEdit>
#include <QHeaderView>
#include <QHBoxLayout>
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
    explicit FootprintPage(CareService& service) : CarePage(QStringLiteral("个人媒体足迹")), service_(service)
    {
        const auto today = care_ui::localTime(QDateTime::currentSecsSinceEpoch()).date();
        auto* bar = new QHBoxLayout;
        mode_->addItems({QStringLiteral("自然周"), QStringLiteral("自然月"), QStringLiteral("自然年"), QStringLiteral("自选区间")});
        mode_->setAccessibleName(QStringLiteral("统计区间类型"));
        from_->setDate(today.addDays(-6));
        to_->setDate(today);
        from_->setAccessibleName(QStringLiteral("开始日期"));
        to_->setAccessibleName(QStringLiteral("结束日期"));
        for (auto* date : {from_, to_}) {
            date->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
            date->setCalendarPopup(true);
            date->setDateRange(QDate(1970, 1, 2), today);
        }
        auto* fetch = new QPushButton(QStringLiteral("查看足迹"));
        bar->addWidget(mode_);
        bar->addWidget(from_);
        bar->addWidget(to_);
        bar->addStretch();
        bar->addWidget(fetch);
        layout->addLayout(bar);
        counts_->setObjectName(QStringLiteral("sectionTitle"));
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
        connect(mode_, &QComboBox::activated, this, [this] { activate(); });
        connect(fetch, &QPushButton::clicked, this, [this] { activate(); });
    }

    void activate() override
    {
        clear();
        invalidate(0);
        const auto today = care_ui::localTime(QDateTime::currentSecsSinceEpoch()).date();
        const auto mode = mode_->currentIndex();
        const bool custom = mode == 3;
        from_->setEnabled(custom);
        to_->setEnabled(custom);
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
        CarePage::deactivate();
        clear();
        status->clear();
    }

private:
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
    QComboBox* mode_ = new QComboBox;
    QDateEdit* from_ = new QDateEdit;
    QDateEdit* to_ = new QDateEdit;
    QLabel* counts_ = new QLabel;
    QLabel* note_ = new QLabel;
    QTableWidget* days_ = new QTableWidget;
    QTableWidget* categories_ = new QTableWidget;
    QTableWidget* people_ = new QTableWidget;
};

} // namespace

CarePage* createFootprintPage(CareService& service) { return new FootprintPage(service); }
