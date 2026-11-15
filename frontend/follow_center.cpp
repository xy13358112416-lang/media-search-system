#include "follow_center.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTabWidget>

using namespace care_contract;

namespace {

class FollowCenter final : public CarePage {
public:
    explicit FollowCenter(CareService& service) : CarePage(QStringLiteral("人物关注中心")), service_(service)
    {
        auto* searchBar = new QHBoxLayout;
        query_->setPlaceholderText(QStringLiteral("输入演员、导演或音乐人姓名"));
        query_->setAccessibleName(QStringLiteral("人物姓名"));
        query_->setMaxLength(MaxQueryUnits);
        auto* search = new QPushButton(QStringLiteral("查找人物"));
        auto* followed = new QPushButton(QStringLiteral("我的关注"));
        searchBar->addWidget(query_, 1);
        searchBar->addWidget(search);
        searchBar->addWidget(followed);
        searchBar->addWidget(more_);
        layout->addLayout(searchBar);

        auto* split = new QHBoxLayout;
        people_->setAccessibleName(QStringLiteral("人物列表"));
        split->addWidget(people_, 2);
        auto* tabs = new QTabWidget;
        tabs->addTab(works_, QStringLiteral("关联作品"));
        tabs->addTab(news_, QStringLiteral("资讯"));
        tabs->addTab(calendar_, QStringLiteral("发布日历"));
        split->addWidget(tabs, 3);
        layout->addLayout(split, 1);
        auto* actions = new QHBoxLayout;
        actions->addWidget(toggle_);
        actions->addStretch();
        actions->addWidget(source_);
        layout->addLayout(actions);

        auto startSearch = [this] {
            submitted_ = query_->text().trimmed();
            if (submitted_.isEmpty()) {
                status->setText(QStringLiteral("请输入人物姓名。"));
                return;
            }
            followingOnly_ = false;
            loadPeople(0);
        };
        connect(search, &QPushButton::clicked, this, startSearch);
        connect(query_, &QLineEdit::returnPressed, this, startSearch);
        connect(followed, &QPushButton::clicked, this, [this] { activate(); });
        connect(more_, &QPushButton::clicked, this, [this] { if (next_) loadPeople(*next_); });
        connect(people_, &QListWidget::currentRowChanged, this, [this] { loadPerson(); });
        connect(toggle_, &QPushButton::clicked, this, [this] { toggleFollow(); });
        connect(news_, &QListWidget::itemSelectionChanged, this, [this] { updateActions(); });
        connect(calendar_, &QListWidget::itemSelectionChanged, this, [this] { updateActions(); });
        connect(tabs, &QTabWidget::currentChanged, this, [this](int index) {
            activeTab_ = index;
            updateActions();
        });
        connect(source_, &QPushButton::clicked, this, [this] {
            const auto url = selectedSource();
            if (url.isValid() && !QDesktopServices::openUrl(url))
                status->setText(QStringLiteral("无法打开来源。"));
        });
        more_->setEnabled(false);
        updateActions();
    }

    void activate() override
    {
        followingOnly_ = true;
        loadPeople(0);
    }

    void deactivate() override
    {
        CarePage::deactivate();
        writing_ = false;
        people_->clear();
        entries_.clear();
        query_->clear();
        next_.reset();
        more_->setEnabled(false);
        status->clear();
    }

private:
    void loadPeople(qint64 offset)
    {
        more_->setEnabled(false);
        if (!offset) {
            entries_.clear();
            people_->clear();
        }
        status->setText(QStringLiteral("正在加载人物…"));
        auto done = latest(0, [this](Result<Page<PersonSummary>> result) {
            if (!result.status.ok() || !result.value) {
                reportError(result);
                return;
            }
            next_ = result.value->nextOffset;
            for (const auto& person : result.value->items) {
                entries_.append(person);
                QStringList roles;
                for (const auto role : person.roles)
                    roles.append(care_ui::roleLabel(role));
                people_->addItem(QStringLiteral("%1 · %2").arg(person.name, roles.join(QStringLiteral("、"))));
            }
            more_->setEnabled(next_.has_value());
            status->setText(entries_.isEmpty() ? QStringLiteral("没有找到人物。") : QString());
            if (people_->currentRow() < 0 && !entries_.isEmpty())
                people_->setCurrentRow(0);
        });
        if (followingOnly_)
            service_.listFollows({offset, 20}, done);
        else
            service_.searchPeople({submitted_, {offset, 20}}, done);
    }

    void loadPerson()
    {
        invalidate(1);
        invalidate(2);
        works_->clear();
        news_->clear();
        calendar_->clear();
        updateActions();
        const auto row = people_->currentRow();
        if (row < 0 || row >= entries_.size())
            return;
        const auto id = entries_.at(row).personId;
        const auto now = care_ui::localTime(QDateTime::currentSecsSinceEpoch()).date();
        service_.getPersonWorks(id, {0, 50}, latest(1, [this](Result<Page<ResourceSummary>> result) {
            if (!result.status.ok() || !result.value) {
                reportError(result);
                return;
            }
            for (const auto& item : result.value->items)
                works_->addItem(item.media.fileName);
            if (works_->count() == 0)
                works_->addItem(QStringLiteral("暂无关联作品"));
        }));
        service_.getPersonUpdates({id, {care_ui::midnight(now.addMonths(-3)),
                                       care_ui::midnight(now.addMonths(6))}, {0, 50}},
            latest(2, [this](Result<Page<PersonUpdate>> result) {
                if (!result.status.ok() || !result.value) {
                    reportError(result);
                    return;
                }
                for (const auto& update : result.value->items) {
                    if (update.kind == UpdateKind::Release && !update.eventAtUtc)
                        continue;
                    auto* list = update.kind == UpdateKind::Release ? calendar_ : news_;
                    const auto date = update.eventAtUtc ? update.eventAtUtc : update.publishedAtUtc;
                    auto* item = new QListWidgetItem(QStringLiteral("%1\n%2 · %3 %4")
                        .arg(update.title, date ? care_ui::timestamp(*date) : QStringLiteral("日期未知"),
                             update.stale ? QStringLiteral("缓存") : QStringLiteral("更新于"),
                             care_ui::timestamp(update.fetchedAtUtc)), list);
                    item->setData(Qt::UserRole, update.sourceUrl);
                }
                if (!news_->count())
                    news_->addItem(QStringLiteral("暂无有来源的资讯"));
                if (!calendar_->count())
                    calendar_->addItem(QStringLiteral("暂无已确认日期的发布"));
            }));
    }

    QUrl selectedSource() const
    {
        const QListWidget* list = activeTab_ == 1 ? news_ : activeTab_ == 2 ? calendar_ : nullptr;
        const auto* item = list ? list->currentItem() : nullptr;
        const auto url = item ? item->data(Qt::UserRole).toUrl() : QUrl();
        return url.scheme() == QStringLiteral("https") && !url.host().isEmpty() ? url : QUrl();
    }

    void updateActions()
    {
        const auto row = people_->currentRow();
        const bool selected = row >= 0 && row < entries_.size();
        toggle_->setEnabled(selected && !writing_);
        toggle_->setText(selected && entries_.at(row).followed ? QStringLiteral("取消关注") : QStringLiteral("关注"));
        const auto url = selectedSource();
        source_->setEnabled(!url.isEmpty());
        source_->setToolTip(url.host());
    }

    void toggleFollow()
    {
        const auto row = people_->currentRow();
        if (row < 0 || row >= entries_.size() || writing_)
            return;
        const auto person = entries_.at(row);
        writing_ = true;
        updateActions();
        service_.setFollow(person.personId, !person.followed, latest(3, [this, person](Status result) {
            writing_ = false;
            updateActions();
            if (!result.ok()) {
                reportError(result);
                return;
            }
            if (followingOnly_)
                loadPeople(0);
            else {
                for (auto& entry : entries_)
                    if (entry.personId == person.personId)
                        entry.followed = !person.followed;
                updateActions();
                status->setText(person.followed ? QStringLiteral("已取消关注。") : QStringLiteral("已关注。"));
            }
        }));
    }

    CareService& service_;
    QLineEdit* query_ = new QLineEdit;
    QListWidget* people_ = new QListWidget;
    QListWidget* works_ = new QListWidget;
    QListWidget* news_ = new QListWidget;
    QListWidget* calendar_ = new QListWidget;
    QPushButton* more_ = new QPushButton(QStringLiteral("加载更多"));
    QPushButton* toggle_ = new QPushButton;
    QPushButton* source_ = new QPushButton(QStringLiteral("打开原始来源"));
    QList<PersonSummary> entries_;
    std::optional<qint64> next_;
    QString submitted_;
    bool followingOnly_ = true;
    bool writing_ = false;
    int activeTab_ = 0;
};

} // namespace

CarePage* createFollowCenter(CareService& service) { return new FollowCenter(service); }
