#include "follow_center.h"

#include <QDesktopServices>
#include <QDialog>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTabWidget>

using namespace care_contract;

namespace {

class FollowCenter final : public CarePage {
public:
    FollowCenter(CareService& service, PersonWorkViewer viewWork)
        : CarePage(QStringLiteral("人物关注中心")), service_(service), viewWork_(std::move(viewWork))
    {
        auto* searchBar = new QHBoxLayout;
        query_->setPlaceholderText(QStringLiteral("输入演员、导演或音乐人姓名"));
        query_->setAccessibleName(QStringLiteral("人物姓名"));
        query_->setMaxLength(MaxQueryUnits);
        auto* search = new QPushButton(QStringLiteral("查找人物"));
        auto* followed = new QPushButton(QStringLiteral("我的关注"));
        searchBar->addWidget(query_, 1);
        searchBar->addWidget(search);
        searchBar->addWidget(webSearch_);
        searchBar->addWidget(followed);
        searchBar->addWidget(more_);
        layout->addLayout(searchBar);

        auto* split = new QHBoxLayout;
        people_->setAccessibleName(QStringLiteral("人物列表"));
        for (auto* list : {people_, works_, news_, calendar_})
            list->setProperty("careList", true);
        split->addWidget(people_, 2);
        auto* tabs = new QTabWidget;
        tabs->setDocumentMode(true);
        tabs->addTab(works_, QStringLiteral("关联作品"));
        tabs->addTab(news_, QStringLiteral("资讯"));
        tabs->addTab(calendar_, QStringLiteral("发布日历"));
        split->addWidget(tabs, 3);
        layout->addLayout(split, 1);
        for (auto* note : {updatesStatus_, workDetails_}) {
            note->setTextFormat(Qt::PlainText);
            note->setWordWrap(true);
            layout->addWidget(note);
        }
        auto* actions = new QHBoxLayout;
        actions->addWidget(toggle_);
        actions->addWidget(openWork_);
        actions->addWidget(worksMore_);
        actions->addWidget(updatesMore_);
        actions->addStretch();
        actions->addWidget(source_);
        layout->addLayout(actions);

        auto startSearch = [this] {
            invalidate(5);
            webSearch_->setEnabled(true);
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
        connect(webSearch_, &QPushButton::clicked, this, [this] { searchWeb(); });
        connect(followed, &QPushButton::clicked, this, [this] { activate(); });
        connect(more_, &QPushButton::clicked, this, [this] { if (next_) loadPeople(*next_); });
        connect(worksMore_, &QPushButton::clicked, this, [this] { if (nextWorks_) loadWorks(*nextWorks_); });
        connect(updatesMore_, &QPushButton::clicked, this, [this] { if (nextUpdates_) loadUpdates(*nextUpdates_); });
        connect(people_, &QListWidget::currentRowChanged, this, [this] { loadPerson(); });
        connect(toggle_, &QPushButton::clicked, this, [this] { toggleFollow(); });
        connect(openWork_, &QPushButton::clicked, this, [this] { openSelectedWork(); });
        connect(works_, &QListWidget::currentRowChanged, this, [this] {
            invalidate(4);
            workDetails_->clear();
            updateActions();
        });
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
        worksMore_->setEnabled(false);
        updatesMore_->setEnabled(false);
        updateActions();
    }

    void activate() override
    {
        invalidate(5);
        webSearch_->setEnabled(true);
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
        submitted_.clear();
        clearDetails();
        next_.reset();
        more_->setEnabled(false);
        status->clear();
        webSearch_->setEnabled(true);
    }

private:
    void searchWeb()
    {
        const auto name = query_->text().trimmed();
        if (name.isEmpty()) {
            status->setText(QStringLiteral("请输入要联网查询的人物姓名。"));
            return;
        }
        invalidate(0);
        webSearch_->setEnabled(false);
        status->setText(QStringLiteral("正在用 DeepSeek 搜索并核对网页原文…"));
        service_.searchPersonWeb(name, latest(5, [this, name](Result<QList<PersonWebSource>> result) {
            webSearch_->setEnabled(true);
            if (!result.status.ok() || !result.value) {
                reportError(result);
                return;
            }
            if (result.value->isEmpty()) {
                status->setText(QStringLiteral("未找到可核对的人物网页，可尝试全名。"));
                return;
            }
            status->clear();
            auto* dialog = new QDialog(this);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->setWindowTitle(QStringLiteral("联网人物来源"));
            dialog->resize(570, 240);
            auto* body = new QVBoxLayout(dialog);
            auto* heading = new QLabel(QStringLiteral("「%1」的外部参考来源").arg(name));
            heading->setTextFormat(Qt::PlainText);
            heading->setWordWrap(true);
            body->addWidget(heading);
            auto* list = new QListWidget;
            list->setWordWrap(true);
            body->addWidget(list);
            for (const auto& source : *result.value) {
                auto* item = new QListWidgetItem(source.title + QStringLiteral("\n") + source.url.host(), list);
                item->setData(Qt::UserRole, source.url);
            }
            auto* open = new QPushButton(QStringLiteral("打开来源网页"));
            body->addWidget(open, 0, Qt::AlignRight);
            list->setCurrentRow(0);
            connect(open, &QPushButton::clicked, dialog, [list] {
                if (const auto* row = list->currentItem())
                    QDesktopServices::openUrl(row->data(Qt::UserRole).toUrl());
            });
            dialog->open();
        }));
    }

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
            status->setText(entries_.isEmpty()
                ? QStringLiteral("人物目录中没有匹配项。可以尝试全名、别名或点击「联网找人物」。")
                : QString());
            if (people_->currentRow() < 0 && !entries_.isEmpty())
                people_->setCurrentRow(0);
        });
        if (followingOnly_)
            service_.listFollows({offset, 20}, done);
        else
            service_.searchPeople({submitted_, {offset, 20}}, done);
    }

    void clearDetails()
    {
        invalidate(1);
        invalidate(2);
        invalidate(4);
        linkedWorks_.clear();
        works_->clear();
        news_->clear();
        calendar_->clear();
        updatesStatus_->clear();
        workDetails_->clear();
        nextWorks_.reset();
        nextUpdates_.reset();
        worksMore_->setEnabled(false);
        updatesMore_->setEnabled(false);
        selectedPersonId_.clear();
        updateActions();
    }

    void loadPerson()
    {
        clearDetails();
        const auto row = people_->currentRow();
        if (row < 0 || row >= entries_.size())
            return;
        selectedPersonId_ = entries_.at(row).personId;
        const auto now = care_ui::localTime(QDateTime::currentSecsSinceEpoch()).date();
        updatesRange_ = {care_ui::midnight(now.addMonths(-3)), care_ui::midnight(now.addMonths(6))};
        loadWorks(0);
        loadUpdates(0);
    }

    void loadWorks(qint64 offset)
    {
        worksMore_->setEnabled(false);
        service_.getPersonWorks(selectedPersonId_, {offset, 50}, latest(1, [this](Result<Page<ResourceSummary>> result) {
            if (!result.status.ok() || !result.value) {
                worksMore_->setEnabled(nextWorks_.has_value());
                reportError(result);
                return;
            }
            nextWorks_ = result.value->nextOffset;
            for (const auto& item : result.value->items) {
                linkedWorks_.append(item.resource.source);
                works_->addItem(item.media.fileName);
            }
            updateActions();
            worksMore_->setEnabled(nextWorks_.has_value());
            if (works_->count() == 0 && !nextWorks_)
                works_->addItem(QStringLiteral("暂无关联作品"));
        }));
    }

    void loadUpdates(qint64 offset)
    {
        updatesMore_->setEnabled(false);
        service_.getPersonUpdates({selectedPersonId_, updatesRange_, {offset, 50}},
            latest(2, [this](Result<Page<PersonUpdate>> result) {
                if (!result.status.ok() || !result.value) {
                    updatesMore_->setEnabled(nextUpdates_.has_value());
                    updatesStatus_->setText(result.status.message.isEmpty()
                        ? QStringLiteral("无法读取人物资讯，请稍后再试。") : result.status.message);
                    return;
                }
                updatesStatus_->clear();
                nextUpdates_ = result.value->nextOffset;
                for (const auto& update : result.value->items) {
                    if (update.kind == UpdateKind::Release && !update.eventAtUtc)
                        continue;
                    auto* list = update.kind == UpdateKind::Release ? calendar_ : news_;
                    const auto date = update.eventAtUtc ? update.eventAtUtc : update.publishedAtUtc;
                    auto* item = new QListWidgetItem(QStringLiteral("%1\n%2 · %3 %4")
                        .arg(update.title, date ? care_ui::timestamp(*date) : QStringLiteral("日期未知"),
                             update.stale ? QStringLiteral("已过期缓存 · 获取于") : QStringLiteral("更新于"),
                             care_ui::timestamp(update.fetchedAtUtc)), list);
                    item->setData(Qt::UserRole, update.sourceUrl);
                }
                updatesMore_->setEnabled(nextUpdates_.has_value());
                if (!news_->count() && !nextUpdates_)
                    news_->addItem(QStringLiteral("暂无有来源的资讯"));
                if (!calendar_->count() && !nextUpdates_)
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

    void openSelectedWork()
    {
        const int row = works_->currentRow();
        if (!viewWork_ || row < 0 || row >= linkedWorks_.size())
            return;
        workDetails_->setText(QStringLiteral("正在读取作品详情…"));
        viewWork_(linkedWorks_.at(row), latest(4, [this](Result<ResourceSummary> result) {
            if (!result.status.ok() || !result.value) {
                workDetails_->setText(result.status.message.isEmpty()
                    ? QStringLiteral("无法读取作品详情。") : result.status.message);
                return;
            }
            const auto& media = result.value->media;
            QStringList details{media.fileName};
            for (const auto& field : {media.category, media.fileType, media.director,
                                      media.releaseDate.toString(Qt::ISODate)})
                if (!field.isEmpty()) details.append(field);
            workDetails_->setText(details.join(QStringLiteral(" · ")));
        }));
    }

    void updateActions()
    {
        const auto row = people_->currentRow();
        const bool selected = row >= 0 && row < entries_.size();
        toggle_->setEnabled(selected && !writing_);
        toggle_->setText(selected && entries_.at(row).followed ? QStringLiteral("取消关注") : QStringLiteral("关注"));
        const int workRow = works_->currentRow();
        openWork_->setEnabled(viewWork_ && activeTab_ == 0 && workRow >= 0 && workRow < linkedWorks_.size());
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
    PersonWorkViewer viewWork_;
    QList<WarehouseItemRef> linkedWorks_;
    QLabel* updatesStatus_ = new QLabel;
    QLabel* workDetails_ = new QLabel;
    QPushButton* openWork_ = new QPushButton(QStringLiteral("查看作品详情"));
    QLineEdit* query_ = new QLineEdit;
    QPushButton* webSearch_ = new QPushButton(QStringLiteral("联网找人物"));
    QListWidget* people_ = new QListWidget;
    QListWidget* works_ = new QListWidget;
    QListWidget* news_ = new QListWidget;
    QListWidget* calendar_ = new QListWidget;
    QPushButton* more_ = new QPushButton(QStringLiteral("加载更多"));
    QPushButton* worksMore_ = new QPushButton(QStringLiteral("加载更多作品"));
    QPushButton* updatesMore_ = new QPushButton(QStringLiteral("加载更多更新"));
    QPushButton* toggle_ = new QPushButton;
    QPushButton* source_ = new QPushButton(QStringLiteral("打开原始来源"));
    QList<PersonSummary> entries_;
    std::optional<qint64> next_;
    std::optional<qint64> nextWorks_;
    std::optional<qint64> nextUpdates_;
    QString selectedPersonId_;
    TimeRange updatesRange_;
    QString submitted_;
    bool followingOnly_ = true;
    bool writing_ = false;
    int activeTab_ = 0;
};

} // namespace

CarePage* createFollowCenter(CareService& service, PersonWorkViewer viewWork)
{
    return new FollowCenter(service, std::move(viewWork));
}
