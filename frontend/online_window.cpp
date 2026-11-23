// >>> DEFENSE-NOTE | 文件导读｜在线界面与检索/今日推荐对接
// 阅读路线：读可用仓库 → 建检索索引 → 提交条件 → 今日候选与洗牌
// 联调关系：Qt负责控件与生命周期；检索/推荐服务返回数据，资源权限仍由服务器核验。
// 标有“答辩导读”的内容仅为讲解注释；原有条件、接口和算法不变。
// <<< DEFENSE-NOTE | 本段讲解结束
#include "online_window.h"
#include "admin_dialog.h"
#include "online_care_client.h"
#include "warehouse_page.h"
#include "assistant_dialog.h"
#include "daily_pick.h"
#include "follow_center.h"
#include "footprint_page.h"
#include "library_style.h"
#include "mediasearch.h"
#include "participation_settings_dialog.h"
#include "temporal_recommendation_page.h"
#include "trending_page.h"
#include <QCloseEvent>
#include <QComboBox>
#include <QDate>
#include <QDialog>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

namespace {
using warehouse_contract::WarehouseService;
using warehouse_contract::UserInfo;
using warehouse_contract::WarehouseSummary;
using warehouse_contract::WarehouseDetail;
using warehouse_contract::WarehouseItem;
using warehouse_contract::Visibility;
using warehouse_contract::RecommendationHandle;
using warehouse_contract::WarehouseRecommendationRequest;
using warehouse_contract::WarehouseRecommendationResult;
using namespace care_contract;
struct CatalogEntry { MediaRecord media; WarehouseItemRef source; };

QTableWidget* createMediaTable(const QString& name)
{
    auto* table = new QTableWidget(0, 5);
    table->setObjectName(name);
    table->setHorizontalHeaderLabels({QStringLiteral("作品名称"), QStringLiteral("类别"),
        QStringLiteral("文件类型"), QStringLiteral("导演"), QStringLiteral("发行时间")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors(true);
    table->setShowGrid(false);
    table->verticalHeader()->hide();
    table->verticalHeader()->setDefaultSectionSize(34);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    return table;
}

QString mediaValue(const QString& value)
{
    return value.trimmed().isEmpty() ? QStringLiteral("—") : value;
}

void setMediaRow(QTableWidget* table, int row, const QString& key, const MediaRecord& media)
{
    const QStringList cells{media.fileName, media.category, media.fileType, media.director,
                            mediaReleaseText(media)};
    for (qsizetype column = 0; column < cells.size(); ++column) {
        auto* item = new QTableWidgetItem(mediaValue(cells.at(column)));
        table->setItem(row, column, item);
    }
    auto* title = table->item(row, 0);
    title->setData(Qt::UserRole, key);
    title->setToolTip(QStringLiteral("<b>%1</b><br>演员或艺人：%2")
        .arg(mediaValue(media.fileName).toHtmlEscaped(),
             mediaValue(media.actors.join(QStringLiteral("、"))).toHtmlEscaped()));
}

class OnlineWindow final : public QWidget {
public:
    OnlineWindow(WarehouseService& warehouses, CareService& care, const UserInfo& user,
                 std::function<void()> logout, QWidget* parent)
        : QWidget(parent), warehouses_(warehouses), care_(care), logout_(std::move(logout))
    {
        setObjectName("libraryWindow");
        setWindowTitle(QStringLiteral("媒体资料库 · 在线"));
        resize(1180, 780);
        setStyleSheet(libraryStyle());
        auto* layout = new QVBoxLayout(this);
        auto* banner = new QFrame;
        banner->setObjectName("banner");
        auto* top = new QHBoxLayout(banner);
        auto* identity = new QLabel(QStringLiteral("媒体资料库 · 服务器用户 %1").arg(user.username));
        identity->setTextFormat(Qt::PlainText);
        identity->setObjectName("appTitle");
        top->addWidget(identity);
        top->addStretch();
        auto* scene = new QComboBox;
        scene->setObjectName("onlineScene");
        scene->addItems({QStringLiteral("默认"),QStringLiteral("放松"),QStringLiteral("专注"),QStringLiteral("探索")});
        top->addWidget(scene);
        connect(scene,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index) {
            warmScene_ = static_cast<warm_mode::Scene>(index);
            scene_ = warm_mode::browseScene(warmScene_);
            clearToday();
            if (tabs_->currentIndex()==2) loadToday();
            if (tabs_->currentIndex()==6 && activeCare_) {
                activeCare_->deactivate(); activeCare_->activate();
            }
        });
        auto* assistant = new QPushButton(QStringLiteral("媒体小助手"));
        auto* settings = new QPushButton(QStringLiteral("参与设置"));
        auto* exit = new QPushButton(QStringLiteral("退出登录"));
        exit->setObjectName("onlineLogout");
        top->addWidget(assistant); top->addWidget(settings);
        if (user.isAdmin) {
            if (auto* client = dynamic_cast<WarehouseClient*>(&warehouses)) {
                auto* admin = new QPushButton(QStringLiteral("管理"));
                admin->setObjectName("onlineAdmin");
                top->addWidget(admin);
                connect(admin, &QPushButton::clicked, this, [this, client] {
                    showAdminDialog(*client, this);
                });
            }
        }
        top->addWidget(exit);
        layout->addWidget(banner);
        status_ = new QLabel;
        status_->setObjectName("onlineCatalogStatus");
        status_->setTextFormat(Qt::PlainText); status_->setWordWrap(true);
        layout->addWidget(status_);
        tabs_ = new QTabWidget;
        tabs_->setObjectName("onlineTabs");
        layout->addWidget(tabs_,1);
        auto* media = new QWidget;
        auto* mediaLayout = new QVBoxLayout(media);
        auto* refresh = new QPushButton(QStringLiteral("刷新在线媒体"));
        mediaLayout->addWidget(refresh, 0, Qt::AlignLeft);
        mediaList_ = createMediaTable(QStringLiteral("onlineMediaList"));
        mediaLayout->addWidget(mediaList_,1);
        tabs_->addTab(media,QStringLiteral("媒体库"));
        auto* search = new QWidget;
        auto* searchLayout = new QVBoxLayout(search);
        auto* fields = new QHBoxLayout;
        auto field = [&](const char* name,const QString& hint) {
            auto* edit = new QLineEdit; edit->setObjectName(name); edit->setPlaceholderText(hint);
            edit->setAccessibleName(hint); edit->setMaxLength(200); fields->addWidget(edit);
            connect(edit,&QLineEdit::returnPressed,this,[this] { submitSearch(); }); return edit;
        };
        title_ = field("onlineSearchTitle",QStringLiteral("作品关键词"));
        director_ = field("onlineSearchDirector",QStringLiteral("导演全名"));
        actor_ = field("onlineSearchActor",QStringLiteral("演员全名"));
        release_ = field("onlineSearchRelease", QStringLiteral("发行时间，如 1981"));
        release_->setMaxLength(10);
        type_ = new QComboBox;
        type_->setObjectName(QStringLiteral("onlineSearchType"));
        type_->setAccessibleName(QStringLiteral("文件类型"));
        type_->addItems({QStringLiteral("全部文件类型"), QStringLiteral("未记录文件类型")});
        fields->addWidget(type_);
        auto* submit = new QPushButton(QStringLiteral("搜索")); submit->setObjectName("onlineSearchSubmit");
        fields->addWidget(submit); searchLayout->addLayout(fields);
        searchStatus_ = new QLabel; searchStatus_->setTextFormat(Qt::PlainText); searchLayout->addWidget(searchStatus_);
        searchList_ = createMediaTable(QStringLiteral("onlineSearchList"));
        searchLayout->addWidget(searchList_,1); tabs_->addTab(search,QStringLiteral("检索"));
        auto* today = new QWidget;
        auto* todayLayout = new QVBoxLayout(today);
        auto* explanation = new QLabel(QStringLiteral("每日更新，点击作品查看详情。"));
        explanation->setWordWrap(true); todayLayout->addWidget(explanation);
        todayStatus_ = new QLabel; todayStatus_->setTextFormat(Qt::PlainText); todayStatus_->setWordWrap(true);
        todayStatus_->setObjectName(QStringLiteral("onlineTodayStatus"));
        todayLayout->addWidget(todayStatus_);
        auto* retryToday = new QPushButton(QStringLiteral("加载今日推荐"));
        todayLayout->addWidget(retryToday);
        todayList_ = new QListWidget; todayList_->setObjectName("onlineTodayList");
        todayList_->setWordWrap(true);
        todayList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        todayLayout->addWidget(todayList_,1); tabs_->addTab(today,QStringLiteral("今日推荐"));
        warehouse_ = new WarehousePage(warehouses_,[this] {
            QList<MediaRecord> records;
            for (const auto& key:order_) records.append(catalog_[key].media);
            return records;
        });
        warehouse_->adoptSession(user);
        tabs_->addTab(warehouse_,QStringLiteral("资源仓库"));
        auto open = [this](const WarehouseItemRef& source) { openWork(source); };
        carePages_ = {createFootprintPage(care_,[this] { return !finished_ && warehouse_->isSignedIn(); }),
                      createTrendingPage(care_,open),
                      createTemporalPage(care_,[this] { return scene_; },open),
                      createFollowCenter(care_,[this](const WarehouseItemRef& source, Callback<ResourceSummary> cb) {
                          warehouse_->recordMediaView(source,std::move(cb));
                      })};
        const QStringList labels{QStringLiteral("个人足迹"),QStringLiteral("全站热榜"),QStringLiteral("动态推荐"),QStringLiteral("关注中心")};
        for (int i=0;i<carePages_.size();++i) tabs_->addTab(carePages_[i],labels[i]);
        connect(tabs_,&QTabWidget::currentChanged,this,[this](int index) {
            ++tabGeneration_;
            if (previousTab_==2 && index!=2) clearToday();
            previousTab_=index;
            if (activeCare_) activeCare_->deactivate();
            activeCare_ = index>=4 ? carePages_.value(index-4) : nullptr;
            if (activeCare_) activeCare_->activate();
            if (index==2) loadToday();
            if (index!=3) warehouse_->suspend();
        });
        for (auto* table : {mediaList_,searchList_}) connect(table,&QTableWidget::cellClicked,this,[this,table](int row, int) {
            const auto* title = table->item(row, 0);
            if (!title)
                return;
            const auto found = catalog_.constFind(title->data(Qt::UserRole).toString());
            if (found != catalog_.cend())
                warehouse_->recordMediaView(found->source);
        });
        connect(todayList_,&QListWidget::itemClicked,this,[this](QListWidgetItem* item) {
            int row=todayList_->row(item); if (row>=0 && row<daily_.size()) openWork(daily_[row].work.resource.source);
        });
        connect(refresh,&QPushButton::clicked,this,[this] { reloadCatalog(); });
        connect(submit,&QPushButton::clicked,this,[this] { submitSearch(); });
        connect(retryToday,&QPushButton::clicked,this,[this] { loadToday(); });
        connect(exit,&QPushButton::clicked,this,[this] { finish(); });
        connect(settings,&QPushButton::clicked,this,[this] {
            auto* dialog=createParticipationSettingsDialog(care_,this);
            connect(dialog,&QDialog::accepted,this,[this] {
                clearToday();
                for (auto* page:carePages_) page->resetSession();
                if (activeCare_) activeCare_->activate();
            });
            dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->open();
        });
        connect(assistant,&QPushButton::clicked,this,[this,scene] {
            auto* dialog = new AssistantDialog(this,[this](const QString& query,media_mood::Mood mood,const QString& category,bool useScene) {
                assistantMood_=useScene ? warm_mode::theme(warmScene_).mood : mood;
                assistantCategory_=category;
                title_->setText(query); tabs_->setCurrentIndex(1); submitSearch(true);
            },[scene](warm_mode::Scene value) { scene->setCurrentIndex(static_cast<int>(value)); },
            [this] { tabs_->setCurrentIndex(2); },QStringLiteral("今日推荐"));
            dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->open();
        });
        warehouse_->setSessionChangedCallback([this] { if (!warehouse_->isSignedIn()) finish(); });
        warehouse_->setItemsChangedCallback([this] { reloadCatalog(); });
        QTimer::singleShot(0,this,[this] { reloadCatalog(); });
    }
    ~OnlineWindow() override { finished_=true; ++generation_; clearToday(); }

    void finish()
    {
        if (finished_) return;
        finished_=true; ++generation_;
        clearToday();
        for (auto* page:carePages_) page->resetSession();
        warehouse_->connectionLost();
        catalog_.clear(); order_.clear(); index_.rebuild({});
        mediaList_->setRowCount(0); searchList_->setRowCount(0); todayList_->clear(); daily_.clear();
        hide();
        const auto callback=logout_;
        if (callback) callback();
    }
protected:
    void closeEvent(QCloseEvent* event) override { event->accept(); finish(); }
private:
    template<class F> auto guard(quint64 generation,F callback) {
        return [self=QPointer<OnlineWindow>(this),generation,callback](auto result) {
            if (self && !self->finished_ && self->generation_==generation) callback(std::move(result));
        };
    }
    void openWork(const WarehouseItemRef& source) {
        if (finished_) return;
        const auto tab = tabGeneration_;
        warehouse_->recordMediaView(source,guard(generation_,[this,tab](Result<ResourceSummary> result) {
            if (tab != tabGeneration_)
                return;
            auto* dialog = new QMessageBox(this);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->setTextFormat(Qt::PlainText);
            if (!result.status.ok() || !result.value) {
                dialog->setWindowTitle(QStringLiteral("无法打开作品"));
                dialog->setText(result.status.message);
                dialog->show();
                return;
            }
            const auto& m=result.value->media;
            dialog->setWindowTitle(QStringLiteral("作品详情"));
            dialog->setText(QStringLiteral("%1\n类别：%2\n导演：%3\n演员或艺人：%4")
                .arg(m.fileName, mediaValue(m.category), mediaValue(m.director),
                     mediaValue(m.actors.join(QStringLiteral("、")))));
            dialog->show();
        }));
    }
    void reloadCatalog() {
        if (finished_) return;
        const auto generation=++generation_;
        catalogReady_=false;
        catalog_.clear(); order_.clear(); index_.rebuild({}); mediaList_->setRowCount(0); searchList_->setRowCount(0);
        searchStatus_->clear();
        status_->setText(QStringLiteral("正在加载服务器公开仓库及我的仓库…"));
        warehouses_.listPublicWarehouses(guard(generation,[this,generation](Result<QList<WarehouseSummary>> result) {
            if (!result.status.ok() || !result.value) { status_->setText(result.status.message); return; }
            const auto publicList=*result.value;
            warehouses_.listMyWarehouses(guard(generation,[this,generation,publicList](Result<QList<WarehouseSummary>> own) {
                if (!own.status.ok() || !own.value) { status_->setText(own.status.message); return; }
                pending_=publicList; QSet<qint64> seen;
                for (const auto& w:pending_) seen.insert(w.warehouseId);
                for (const auto& w:*own.value) if (!seen.contains(w.warehouseId)) { pending_.append(w); seen.insert(w.warehouseId); }
                pendingPosition_=0; failedDetails_=0; loadNext(generation);
            }));
        }));
    }
    // >>> DEFENSE-NOTE | loadNext = 汇总仓库详情并建立在线检索索引
    // 读取服务器允许的公共仓库和本人仓库；每项用“仓库/条目”构造来源key。
    // 全部加载后将媒体副本id设为key并rebuild；不是从浏览次数猜权限。
    // <<< DEFENSE-NOTE | 本段讲解结束
    void loadNext(quint64 generation) {
        if (finished_ || generation!=generation_) return;
        if (pendingPosition_>=pending_.size()) {
            QList<MediaRecord> records;
            for (const auto& key:order_) { auto record=catalog_[key].media; record.id=key; records.append(record); }
            index_.rebuild(records);
            catalogReady_=true;
            QStringList types;
            for (const auto& record : records) {
                const auto type = record.fileType.trimmed().toUpper();
                if (!type.isEmpty() && !types.contains(type))
                    types.append(type);
            }
            types.sort();
            const auto selectedType = type_->currentText();
            type_->clear();
            type_->addItems({QStringLiteral("全部文件类型"), QStringLiteral("未记录文件类型")});
            type_->addItems(types);
            type_->setCurrentIndex(qMax(0, type_->findText(selectedType)));
            mediaList_->setRowCount(order_.size());
            for (qsizetype row = 0; row < order_.size(); ++row) {
                const auto& key = order_.at(row);
                setMediaRow(mediaList_, row, key, catalog_[key].media);
            }
            status_->setText(QStringLiteral("在线可读作品 %1 条；未能读取的仓库 %2 个。权限由服务器核验。").arg(catalog_.size()).arg(failedDetails_));
            return;
        }
        const auto id=pending_.at(pendingPosition_++).warehouseId;
        warehouses_.getWarehouse(id,guard(generation,[this,generation,id](Result<WarehouseDetail> result) {
            if (result.status.ok() && result.value && result.value->warehouse.warehouseId==id) {
                for (const auto& item:result.value->items) {
                    const auto key=QStringLiteral("%1/%2").arg(id).arg(item.itemId);
                    if (!catalog_.contains(key)) order_.append(key);
                    catalog_.insert(key,{item.media,{id,item.itemId}});
                }
            } else ++failedDetails_;
            // Queue the next request to avoid recursive stack growth with synchronous services.
            QTimer::singleShot(0,this,[this,generation] { loadNext(generation); });
        }));
    }
    // >>> DEFENSE-NOTE | submitSearch = 把输入交给索引并展示
    // 导演/演员/格式先交MediaSearch，标题/助手类别与情绪在候选结果上继续过滤。
    // 搜索行为通过服务接口另存；行为保存失败不等于已经显示的检索计算失败。
    // <<< DEFENSE-NOTE | 本段讲解结束
    void submitSearch(bool fromAssistant=false) {
        if (finished_) return;
        if (!catalogReady_) {
            searchStatus_->setText(QStringLiteral("媒体目录尚未就绪，请加载完成后再搜索。"));
            return;
        }
        if (!fromAssistant) { assistantMood_=media_mood::Mood::Any; assistantCategory_.clear(); }
        const bool unknownType = type_->currentIndex() == 1;
        SearchQuery query{director_->text().trimmed(), actor_->text().trimmed(),
                          type_->currentIndex() >= 2 ? type_->currentText() : QString()};
        const auto title=title_->text().trimmed();
        const auto period = release_->text().trimmed();
        MediaRecord checkedDate;
        if (!parseMediaRelease(period, checkedDate)) {
            searchList_->setRowCount(0);
            searchStatus_->setText(QStringLiteral("请输入有效的发行年份、年月或日期，如 1981、1981-05 或 1981-05-22。"));
            return;
        }
        QStringList terms;
        if (!title.isEmpty()) terms<<title;
        if (!query.director.isEmpty()) terms<<QStringLiteral("导演:%1").arg(query.director);
        if (!query.actor.isEmpty()) terms<<QStringLiteral("演员:%1").arg(query.actor);
        if (!query.fileType.isEmpty()) terms<<QStringLiteral("格式:%1").arg(query.fileType);
        if (unknownType) terms << QStringLiteral("格式:未记录");
        if (!period.isEmpty()) terms << QStringLiteral("发行时间:%1").arg(period);
        if (!assistantCategory_.isEmpty()) terms<<QStringLiteral("类别:%1").arg(assistantCategory_);
        if (assistantMood_!=media_mood::Mood::Any) terms<<media_mood::label(assistantMood_);
        if (terms.isEmpty()) { searchStatus_->setText(QStringLiteral("请填写至少一个搜索条件。")); return; }
        BehaviorEvent event; event.eventId=QUuid::createUuid().toString(QUuid::WithoutBraces);
        event.action=BehaviorAction::Search; event.query=terms.join(QStringLiteral(" ")).left(MaxQueryUnits);
        searchList_->setRowCount(0);
        const auto hits=index_.search(query);
        for (const auto& key:hits) if (catalog_.contains(key) && catalog_[key].media.fileName.contains(title,Qt::CaseInsensitive)
            && (!unknownType || catalog_[key].media.fileType.trimmed().isEmpty())
            && (period.isEmpty() || mediaReleaseText(catalog_[key].media).startsWith(period))
            && (assistantCategory_.isEmpty() || catalog_[key].media.category==assistantCategory_)
            && media_mood::matches(catalog_[key].media,assistantMood_)) {
            const int row = searchList_->rowCount();
            searchList_->insertRow(row);
            setMediaRow(searchList_, row, key, catalog_[key].media);
        }
        searchStatus_->setText(QStringLiteral("共 %1 条结果").arg(searchList_->rowCount()));
        care_.recordBehavior(event,guard(generation_,[this](Status status) {
            if (!status.ok()) searchStatus_->setText(QStringLiteral("搜索记录未保存：%1").arg(status.message));
        }));
    }
    // >>> DEFENSE-NOTE | loadToday = 在线今日推荐的总入口
    // 先请求最多30个时序/场景候选，结果按workId去重，再按北京时间日期种子dailyPick最多6个。
    // 不是直接从全库随机抽六条；新版非默认场景可能走服务端实时模型，需以返回状态判断是否回退。
    // QPointer和todayGeneration_防页面销毁、换号或旧请求覆盖新结果。
    // <<< DEFENSE-NOTE | 本段讲解结束
    void loadToday() {
        if (finished_ || todayLoading_) return;
        const auto date=care_ui::localTime(QDateTime::currentSecsSinceEpoch()).date();
        todayLoading_=true;
        const auto todayGeneration=++todayGeneration_;
        const bool liveScene=scene_!=BrowseScene::Neutral;
        todayStatus_->setText(liveScene?QStringLiteral("正在实时查询场景推荐…"):QStringLiteral("正在加载今日候选…"));
        TemporalRecommendationRequest request; request.limit=liveScene?6:30; request.scene=scene_;
        const auto handle=care_.recommendTemporal(request,[self=QPointer<OnlineWindow>(this),date,todayGeneration,liveScene](Result<TemporalRecommendationResult> result) {
            if (!self || self->finished_ || self->todayGeneration_!=todayGeneration) return;
            self->todayLoading_=false;
            self->todayHandle_.clear();
            if (!result.status.ok() || !result.value) {
                self->todayStatus_->setText(QStringLiteral("暂时无法加载今日推荐，请重试。"));
                return;
            }
            QHash<QString,RecommendedResource> pool; QStringList keys;
            for (const auto& item:result.value->items) {
                const auto key=item.work.resource.workId;
                if (!key.isEmpty() && !pool.contains(key)) { keys.append(key); pool.insert(key,item); }
            }
            self->daily_.clear(); self->todayList_->clear();
            const auto picked=liveScene?keys.mid(0,6):dailyPick(keys,static_cast<quint64>(date.toJulianDay()),6);
            for (const auto& key:picked) {
                const auto item=pool.value(key); self->daily_.append(item);
                new QListWidgetItem(item.work.media.fileName,self->todayList_);
            }
            self->dailyDate_=date;
            QStringList notices;
            if (liveScene && !result.value->message.isEmpty()) notices.append(result.value->message);
            if (self->daily_.isEmpty())
                notices.append(QStringLiteral("暂无可推荐的作品。"));
            if (result.value->fallback)
                notices.append(QStringLiteral("已使用备用推荐。"));
            if (result.value->containsSyntheticData)
                notices.append(QStringLiteral("含模拟数据"));
            self->todayStatus_->setText(notices.join(QStringLiteral(" · ")));
        });
        if (todayLoading_ && todayGeneration_==todayGeneration) todayHandle_=handle;
    }
    // >>> DEFENSE-NOTE | clearToday = 清除每日结果并取消在途请求
    // 先增加代次，使旧回调失效，再清结果和句柄并调用取消；切号后不应串用上一用户的推荐。
    // <<< DEFENSE-NOTE | 本段讲解结束
    void clearToday() {
        ++todayGeneration_; todayLoading_=false; dailyDate_=QDate(); daily_.clear();
        if (todayList_) todayList_->clear();
        const auto handle=std::exchange(todayHandle_,{});
        if (!handle.isEmpty()) care_.cancelRecommendation(handle);
    }
    WarehouseService& warehouses_; CareService& care_; std::function<void()> logout_;
    WarehousePage* warehouse_=nullptr; QList<CarePage*> carePages_; CarePage* activeCare_=nullptr;
    QTabWidget* tabs_; QTableWidget *mediaList_, *searchList_; QListWidget* todayList_;
    QLineEdit *title_, *director_, *actor_, *release_;
    QComboBox* type_;
    QLabel *status_,*searchStatus_,*todayStatus_;
    QHash<QString,CatalogEntry> catalog_; QStringList order_; MediaSearch index_;
    bool catalogReady_=false;
    QList<WarehouseSummary> pending_; int pendingPosition_=0,failedDetails_=0;
    QList<RecommendedResource> daily_; QDate dailyDate_; bool todayLoading_=false,finished_=false;
    BrowseScene scene_=BrowseScene::Neutral; quint64 generation_=0;
    warm_mode::Scene warmScene_=warm_mode::Scene::Neutral;
    media_mood::Mood assistantMood_=media_mood::Mood::Any; QString assistantCategory_;
    quint64 todayGeneration_=0; RecommendationHandle todayHandle_;
    int previousTab_=0;
    quint64 tabGeneration_=0;
};
}

QWidget* createOnlineWindow(WarehouseService& warehouses,CareService& care,const UserInfo& user,
                          std::function<void()> logout,QWidget* parent)
{ return new OnlineWindow(warehouses,care,user,std::move(logout),parent); }

QWidget* createOnlineWindow(OnlineCareClient& client,const UserInfo& user,
                          std::function<void()> logout,QWidget* parent)
{
    auto* window=new OnlineWindow(client,client,user,std::move(logout),parent);
    client.onDisconnected(window,[window] { window->finish(); });
    return window;
}
