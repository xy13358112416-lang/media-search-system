#include "trending_page.h"

#include <QComboBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QPushButton>
#include <QTableWidget>

using namespace care_contract;

namespace {

class TrendingPage final : public CarePage {
public:
    explicit TrendingPage(CareService& service) : CarePage(QStringLiteral("全站热榜")), service_(service)
    {
        auto* filters = new QHBoxLayout;
        period_->addItems({QStringLiteral("今日"), QStringLiteral("本周")});
        period_->setAccessibleName(QStringLiteral("热榜时间范围"));
        kind_->addItems({QStringLiteral("热门作品"), QStringLiteral("热门搜索词")});
        kind_->setAccessibleName(QStringLiteral("榜单类型"));
        auto* refresh = new QPushButton(QStringLiteral("刷新热榜"));
        filters->addWidget(period_);
        filters->addWidget(kind_);
        filters->addStretch();
        filters->addWidget(refresh);
        layout->addLayout(filters);
        layout->addWidget(new QLabel(QStringLiteral("北京时间 · 同一项至少有 3 位用户公开贡献才展示 · 每人每项每日最多计一次。")));
        summary_->setTextFormat(Qt::PlainText);
        summary_->setWordWrap(true);
        layout->addWidget(summary_);
        table_->setObjectName(QStringLiteral("careTrendingTable"));
        table_->setHorizontalHeaderLabels({QStringLiteral("排名"), QStringLiteral("作品或热词"),
            QStringLiteral("独立用户"), QStringLiteral("有效行为")});
        table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        table_->verticalHeader()->hide();
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table_->setAlternatingRowColors(true);
        layout->addWidget(table_, 1);
        connect(refresh, &QPushButton::clicked, this, [this] { activate(); });
        connect(period_, &QComboBox::activated, this, [this] { activate(); });
        connect(kind_, &QComboBox::activated, this, [this] { activate(); });
    }

    void activate() override
    {
        clear();
        status->setText(QStringLiteral("正在加载热榜…"));
        const auto date = care_ui::localTime(QDateTime::currentSecsSinceEpoch()).date();
        const auto start = period_->currentIndex() == 0 ? date : date.addDays(1 - date.dayOfWeek());
        const auto end = start.addDays(period_->currentIndex() == 0 ? 1 : 7);
        const auto kind = kind_->currentIndex() == 0 ? TrendingKind::Works : TrendingKind::SearchTerms;
        service_.getTrending({kind, {care_ui::midnight(start), care_ui::midnight(end)}, 20},
            latest(0, [this](Result<TrendingResult> result) {
                if (!result.status.ok() || !result.value) {
                    reportError(result);
                    return;
                }
                const auto& data = *result.value;
                const auto period = QStringLiteral("%1 至 %2")
                    .arg(care_ui::localTime(data.range.beginUtc).date().toString(Qt::ISODate),
                         care_ui::localTime(data.range.endUtc - 1).date().toString(Qt::ISODate));
                status->setText(QStringLiteral("%1 · 生成于 %2 · %3 条%4")
                    .arg(period, care_ui::timestamp(data.generatedAtUtc)).arg(data.items.size())
                    .arg(data.containsSyntheticData ? QStringLiteral(" · 含模拟数据") : QString()));
                summary_->setText(data.summary.isEmpty()
                    ? (data.items.isEmpty() ? QStringLiteral("当前时段暂无公开热榜。") : QString())
                    : QStringLiteral("%1 · %2").arg(data.summaryFromModel ? QStringLiteral("AI 摘要") : QStringLiteral("榜单摘要"),
                                                  data.summary));
                table_->setRowCount(data.items.size());
                for (int row = 0; row < data.items.size(); ++row) {
                    const auto& entry = data.items.at(row);
                    const QStringList cells{QString::number(row + 1), entry.work ? entry.work->media.fileName : entry.term,
                                            QString::number(entry.distinctUsers), QString::number(entry.effectiveEvents)};
                    for (int col = 0; col < cells.size(); ++col)
                        table_->setItem(row, col, new QTableWidgetItem(cells.at(col)));
                }
            }));
    }

    void deactivate() override
    {
        CarePage::deactivate();
        clear();
        status->clear();
    }
private:
    void clear()
    {
        table_->setRowCount(0);
        summary_->clear();
    }

    CareService& service_;
    QComboBox* period_ = new QComboBox;
    QComboBox* kind_ = new QComboBox;
    QLabel* summary_ = new QLabel;
    QTableWidget* table_ = new QTableWidget(0, 4);
};

} // namespace

CarePage* createTrendingPage(CareService& service) { return new TrendingPage(service); }
