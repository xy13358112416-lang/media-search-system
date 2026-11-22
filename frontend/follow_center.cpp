// >>> DEFENSE-NOTE | 文件导读｜人物关注页面
// 阅读路线：人物检索 → 关注/取关 → 作品/资讯分页
// 联调关系：人物关联和权限来自服务端可信目录；姓名仅展示，personId才是操作键。
// 标有“答辩导读”的内容仅为讲解注释；原有条件、接口和算法不变。
// <<< DEFENSE-NOTE | 本段讲解结束
#include "follow_center.h"

#include <QDesktopServices>
#include <QDialog>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QComboBox>

using namespace care_contract;

namespace {

class FollowCenter final : public CarePage {
public:
    FollowCenter(CareService& service, PersonWorkViewer viewWork)
        : CarePage(QStringLiteral("人物与作品")), service_(service), viewWork_(std::move(viewWork))
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
        for (auto* list : {people_, works_})
            list->setProperty("careList", true);
        split->addWidget(people_, 2);
        auto* tabs = new QTabWidget;
        tabs->setDocumentMode(true);
        tabs->addTab(works_, QStringLiteral("关联作品"));
        // This demo presents verified people and works; no empty news/calendar tabs.
        auto* profile = new QWidget;
        auto* profileLayout = new QVBoxLayout(profile);
        profileTitle_->setTextFormat(Qt::PlainText);
        profileTitle_->setWordWrap(true);
        profileTitle_->setText(QStringLiteral("选择人物，查看本库作品；联网资料用于了解外部代表作品。"));
        profileLayout->addWidget(profileTitle_);
        profileLayout->addWidget(tabs);
        split->addWidget(profile, 3);
        layout->addLayout(split, 1);
        for (auto* note : {workDetails_}) {
            note->setTextFormat(Qt::PlainText);
            note->setWordWrap(true);
            layout->addWidget(note);
        }
        auto* actions = new QHBoxLayout;
        actions->addWidget(toggle_);
        actions->addWidget(openWork_);
        actions->addWidget(worksMore_);
        actions->addStretch();
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
        connect(people_, &QListWidget::currentRowChanged, this, [this] { loadPerson(); });
        connect(toggle_, &QPushButton::clicked, this, [this] { toggleFollow(); });
        connect(openWork_, &QPushButton::clicked, this, [this] { openSelectedWork(); });
        connect(works_, &QListWidget::currentRowChanged, this, [this] {
            invalidate(4);
            workDetails_->clear();
            updateActions();
        });
        connect(tabs, &QTabWidget::currentChanged, this, [this](int index) {
            activeTab_ = index;
            updateActions();
        });
        more_->setEnabled(false);
        worksMore_->setEnabled(false);
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
                status->setText(QStringLiteral("联网补充暂时不可用，已加载的本库人物和作品会保留。 ") + result.status.message);
                return;
            }
            if (result.value->isEmpty()) {
                status->setText(QStringLiteral("未找到可核对的人物网页，可尝试全名。"));
                return;
            }
            status->clear();
            auto* dialog = new QDialog(this);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->setWindowTitle(QStringLiteral("人物主页 · 外部参考资料"));
            dialog->resize(920, 640);
            auto* body = new QVBoxLayout(dialog);
            auto* choices = new QComboBox;
            for (const auto& person : *result.value) choices->addItem(person.title);
            body->addWidget(choices);
            auto* biography = new QLabel;
            biography->setTextFormat(Qt::PlainText); biography->setWordWrap(true);
            biography->setTextInteractionFlags(Qt::TextSelectableByMouse);
            body->addWidget(biography);
            auto* notice = new QLabel(QStringLiteral("联网模型根据搜索来源整理，请核对原文。这些是外部作品资料，不代表本库拥有或可播放。"));
            notice->setWordWrap(true); body->addWidget(notice);
            auto* table = new QTableWidget(0, 4);
            table->setHorizontalHeaderLabels({QStringLiteral("代表作品"), QStringLiteral("年份"), QStringLiteral("参与角色"), QStringLiteral("资料来源")});
            table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
            table->setEditTriggers(QAbstractItemView::NoEditTriggers);
            table->setSelectionBehavior(QAbstractItemView::SelectRows);
            body->addWidget(table, 1);
            auto* empty = new QLabel; body->addWidget(empty);
            auto* openPerson = new QPushButton(QStringLiteral("核对人物来源"));
            body->addWidget(openPerson);
            const auto profiles = *result.value;
            auto display = [profiles, choices, biography, table, empty] {
                const int index = choices->currentIndex();
                if (index < 0 || index >= profiles.size()) return;
                const auto& person = profiles.at(index);
                biography->setText(person.title + QStringLiteral("\n") + (person.biography.isEmpty() ? QStringLiteral("暂无可靠简介。") : person.biography));
                table->setRowCount(person.works.size());
                for (int row=0; row<person.works.size(); ++row) {
                    const auto& work = person.works.at(row);
                    const QStringList values{work.title, work.year.isEmpty()?QStringLiteral("未知"):work.year,
                        work.role.isEmpty()?QStringLiteral("未确认"):work.role, work.sourceUrl.host()};
                    for (int col=0; col<values.size(); ++col) {
                        auto* cell = new QTableWidgetItem(values.at(col)); cell->setData(Qt::UserRole, work.sourceUrl);
                        table->setItem(row, col, cell);
                    }
                }
                empty->setText(person.works.isEmpty()?QStringLiteral("本次没有取得有来源的作品列表，可重试或核对人物来源。"):QStringLiteral("双击作品可核对来源；姓名下拉可选择同名人物。"));
            };
            connect(choices, &QComboBox::currentIndexChanged, dialog, [display](int) { display(); });
            connect(openPerson, &QPushButton::clicked, dialog, [profiles, choices] {
                const int index=choices->currentIndex(); if(index>=0 && index<profiles.size()) QDesktopServices::openUrl(profiles.at(index).url);
            });
            connect(table, &QTableWidget::cellDoubleClicked, dialog, [table](int row,int col) {
                if (auto* cell=table->item(row,col)) QDesktopServices::openUrl(cell->data(Qt::UserRole).toUrl());
            });
            display();
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
        workDetails_->clear();
        nextWorks_.reset();
        worksMore_->setEnabled(false);
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
        loadWorks(0);
        profileTitle_->setText(entries_.at(row).name + (selectedPersonId_.startsWith("field:")
            ? QStringLiteral(" · 媒体字段中的姓名，身份未核实；仅按原字段归组作品，可联网核对。")
            : QStringLiteral(" · 已核实人物 · 本库可读作品")));
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
        toggle_->setEnabled(selected && !writing_ && !entries_.at(row).personId.startsWith("field:"));
        toggle_->setText(selected && entries_.at(row).followed ? QStringLiteral("取消关注") : QStringLiteral("关注"));
        const int workRow = works_->currentRow();
        openWork_->setEnabled(viewWork_ && activeTab_ == 0 && workRow >= 0 && workRow < linkedWorks_.size());
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
    QLabel* profileTitle_ = new QLabel;
    QLabel* workDetails_ = new QLabel;
    QPushButton* openWork_ = new QPushButton(QStringLiteral("查看作品详情"));
    QLineEdit* query_ = new QLineEdit;
    QPushButton* webSearch_ = new QPushButton(QStringLiteral("联网人物主页"));
    QListWidget* people_ = new QListWidget;
    QListWidget* works_ = new QListWidget;
    QPushButton* more_ = new QPushButton(QStringLiteral("加载更多"));
    QPushButton* worksMore_ = new QPushButton(QStringLiteral("加载更多作品"));
    QPushButton* toggle_ = new QPushButton;
    QList<PersonSummary> entries_;
    std::optional<qint64> next_;
    std::optional<qint64> nextWorks_;
    QString selectedPersonId_;
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
