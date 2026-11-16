#include "warehouse_page.h"

#include <algorithm>

#include <QCheckBox>
#include <QDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QUuid>
#include <utility>

using namespace warehouse_contract;

namespace {
QString itemLabel(const MediaRecord& record)
{
    return record.category.isEmpty() ? record.fileName
        : QStringLiteral("%1 · %2").arg(record.fileName, record.category);
}
}

WarehousePage::WarehousePage(WarehouseService& service,
                             std::function<QList<MediaRecord>()> localRecords, QWidget* parent)
    : QWidget(parent), service_(service), care_(dynamic_cast<care_contract::CareService*>(&service)),
      localRecords_(std::move(localRecords)),
      host_(new QLineEdit(QStringLiteral("127.0.0.1"))), port_(new QSpinBox),
      username_(new QLineEdit), password_(new QLineEdit),
      identity_(new QLabel), status_(new QLabel), login_(new QPushButton(QStringLiteral("连接并登录"))),
      logout_(new QPushButton(QStringLiteral("退出仓库"))), refresh_(new QPushButton(QStringLiteral("刷新列表"))),
      create_(new QPushButton(QStringLiteral("新建仓库"))), add_(new QPushButton(QStringLiteral("添加本地作品"))),
      remove_(new QPushButton(QStringLiteral("删除条目"))), recommend_(new QPushButton(QStringLiteral("获取推荐"))),
      cancel_(new QPushButton(QStringLiteral("取消推荐"))),
      addSuggestion_(new QPushButton(QStringLiteral("确认加入所选仓库"))),
      warehouseList_(new QListWidget), itemList_(new QListWidget), recommendationList_(new QListWidget),
      publicOnly_(new QCheckBox(QStringLiteral("查看公开仓库"))),
      useTarget_(new QCheckBox(QStringLiteral("使用所选仓库"))), query_(new QLineEdit), reason_(new QLabel)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    port_->setRange(1, 65535);
    port_->setValue(8765);
    host_->setAccessibleName(QStringLiteral("服务器地址"));
    port_->setAccessibleName(QStringLiteral("服务器端口"));
    username_->setAccessibleName(QStringLiteral("网络用户名"));
    password_->setAccessibleName(QStringLiteral("网络密码"));
    query_->setAccessibleName(QStringLiteral("推荐需求"));
    host_->setObjectName(QStringLiteral("warehouseHost"));
    port_->setObjectName(QStringLiteral("warehousePort"));
    username_->setObjectName(QStringLiteral("warehouseUsername"));
    password_->setObjectName(QStringLiteral("warehousePassword"));
    warehouseList_->setObjectName(QStringLiteral("onlineWarehouseList"));
    itemList_->setObjectName(QStringLiteral("onlineWarehouseItems"));
    recommendationList_->setObjectName(QStringLiteral("onlineWarehouseRecommendations"));
    query_->setObjectName(QStringLiteral("warehouseRecommendationQuery"));
    status_->setObjectName(QStringLiteral("warehouseStatus"));
    password_->setEchoMode(QLineEdit::Password);
    username_->setPlaceholderText(QStringLiteral("网络用户名"));
    password_->setPlaceholderText(QStringLiteral("网络密码"));
    query_->setPlaceholderText(QStringLiteral("例如：想找一部轻松的电影"));
    query_->setMaxLength(1000);
    useTarget_->setChecked(true);
    login_->setObjectName(QStringLiteral("primary"));
    reason_->setTextFormat(Qt::PlainText);
    reason_->setWordWrap(true);
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    identity_->setTextFormat(Qt::PlainText);

    auto* connection = new QHBoxLayout;
    connection->addWidget(new QLabel(QStringLiteral("服务器")));
    connection->addWidget(host_, 2);
    connection->addWidget(port_);
    connection->addWidget(username_, 1);
    connection->addWidget(password_, 1);
    connection->addWidget(login_);
    connection->addWidget(logout_);
    layout->addLayout(connection);
    layout->addWidget(identity_);
    layout->addWidget(status_);

    auto* toolbar = new QHBoxLayout;
    toolbar->addWidget(publicOnly_);
    toolbar->addWidget(refresh_);
    toolbar->addWidget(create_);
    toolbar->addStretch();
    toolbar->addWidget(add_);
    toolbar->addWidget(remove_);
    layout->addLayout(toolbar);

    publicSearchBar_ = new QWidget;
    auto* searchBar = new QHBoxLayout(publicSearchBar_);
    searchBar->setContentsMargins(0, 0, 0, 0);
    publicSearch_ = new QLineEdit;
    publicSearch_->setObjectName(QStringLiteral("warehousePublicSearch"));
    publicSearch_->setAccessibleName(QStringLiteral("当前公开仓库的作品关键字"));
    publicSearch_->setPlaceholderText(QStringLiteral("片名、类别、导演或演员"));
    publicSearch_->setMaxLength(care_contract::MaxQueryUnits);
    submitSearch_ = new QPushButton(QStringLiteral("搜索当前公开仓库"));
    submitSearch_->setToolTip(QStringLiteral("搜索历史和公开热榜贡献遵循当前服务器账号的参与设置。"));
    auto* clearSearch = new QPushButton(QStringLiteral("清除搜索"));
    searchBar->addWidget(publicSearch_, 1);
    searchBar->addWidget(submitSearch_);
    searchBar->addWidget(clearSearch);
    layout->addWidget(publicSearchBar_);
    searchStatus_ = new QLabel;
    searchStatus_->setTextFormat(Qt::PlainText);
    searchStatus_->setWordWrap(true);
    layout->addWidget(searchStatus_);
    connect(submitSearch_, &QPushButton::clicked, this, [this] { searchPublicWarehouse(); });
    connect(publicSearch_, &QLineEdit::returnPressed, this, [this] { searchPublicWarehouse(); });
    connect(publicSearch_, &QLineEdit::textChanged, this, [this] { updateControls(); });
    connect(clearSearch, &QPushButton::clicked, this, [this] {
        ++searchGeneration_;
        publicSearch_->clear();
        activeSearch_.clear();
        applyPublicSearch();
    });

    auto* panes = new QHBoxLayout;
    const auto addPane = [&](const QString& title, QListWidget* list, int stretch) {
        auto* column = new QVBoxLayout;
        auto* label = new QLabel(title);
        label->setObjectName(QStringLiteral("sectionTitle"));
        list->setAccessibleName(title);
        column->addWidget(label);
        column->addWidget(list, 1);
        panes->addLayout(column, stretch);
    };
    addPane(QStringLiteral("仓库"), warehouseList_, 2);
    addPane(QStringLiteral("条目"), itemList_, 3);
    addPane(QStringLiteral("推荐建议"), recommendationList_, 3);
    layout->addLayout(panes, 1);

    auto* recommendationBar = new QHBoxLayout;
    recommendationBar->addWidget(query_, 1);
    recommendationBar->addWidget(useTarget_);
    recommendationBar->addWidget(recommend_);
    recommendationBar->addWidget(cancel_);
    layout->addLayout(recommendationBar);
    recommendationStatus_ = new QLabel;
    recommendationStatus_->setObjectName(QStringLiteral("warehouseRecommendationStatus"));
    recommendationStatus_->setTextFormat(Qt::PlainText);
    recommendationStatus_->setWordWrap(true);
    layout->addWidget(recommendationStatus_);
    auto* recommendationFooter = new QHBoxLayout;
    recommendationFooter->addWidget(reason_, 1);
    recommendationFooter->addWidget(addSuggestion_);
    layout->addLayout(recommendationFooter);

    connect(login_, &QPushButton::clicked, this, [this] { connectAndLogin(); });
    connect(password_, &QLineEdit::returnPressed, this, [this] { connectAndLogin(); });
    connect(logout_, &QPushButton::clicked, this, [this] { signOut(); });
    connect(refresh_, &QPushButton::clicked, this, [this] { refresh(); });
    connect(create_, &QPushButton::clicked, this, [this] { createWarehouse(); });
    connect(add_, &QPushButton::clicked, this, [this] { addLocalMedia(); });
    connect(remove_, &QPushButton::clicked, this, [this] { removeItem(); });
    connect(publicOnly_, &QCheckBox::toggled, this, [this] { refresh(); });
    connect(warehouseList_, &QListWidget::currentRowChanged, this, [this] {
        clearRecommendation();
        refreshDetail();
    });
    connect(itemList_, &QListWidget::currentRowChanged, this, [this] { updateControls(); });
    connect(itemList_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        const auto row = itemList_->row(item);
        const auto warehouse = selection();
        if (warehouse && row >= 0 && row < items_.size())
            recordMediaView({warehouse->warehouseId, items_.at(row).itemId});
    });
    connect(recommendationList_, &QListWidget::currentRowChanged, this, [this](int row) {
        reason_->setText(row >= 0 && row < suggestions_.size() ? suggestions_.at(row).reason : QString());
        updateControls();
    });
    connect(recommendationList_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        const auto row = recommendationList_->row(item);
        if (row >= 0 && row < suggestions_.size()) {
            const auto& choice = suggestions_.at(row);
            recordMediaView({choice.warehouseId, choice.itemId});
        }
    });
    connect(useTarget_, &QCheckBox::toggled, this, [this] { clearRecommendation(); });
    connect(recommend_, &QPushButton::clicked, this, [this] { recommend(); });
    connect(query_, &QLineEdit::returnPressed, this, [this] { recommend(); });
    connect(cancel_, &QPushButton::clicked, this, [this] {
        clearRecommendation();
        recommendationStatus_->setText(QStringLiteral("已取消推荐。"));
    });
    connect(addSuggestion_, &QPushButton::clicked, this, [this] { addSuggestion(); });
    updateControls();
}

WarehousePage::~WarehousePage()
{
    ++sessionEpoch_;
    user_.reset();
    service_.disconnectFromServer();
}

std::optional<WarehouseSummary> WarehousePage::selection() const
{
    const int row = warehouseList_->currentRow();
    if (row >= 0 && row < warehouses_.size())
        return warehouses_.at(row);
    return std::nullopt;
}

bool WarehousePage::ownsSelection() const
{
    const auto warehouse = selection();
    return warehouse && user_ && warehouse->ownerId == user_->userId;
}

void WarehousePage::updateControls()
{
    const bool signedIn = user_.has_value();
    for (auto* input : QList<QWidget*>{host_, port_, username_, password_})
        input->setEnabled(!signedIn && !connecting_);
    login_->setEnabled(!signedIn && !connecting_);
    logout_->setEnabled(signedIn || connecting_);
    refresh_->setEnabled(signedIn && !writing_);
    publicOnly_->setEnabled(signedIn && !writing_);
    create_->setEnabled(signedIn && !writing_);
    const auto warehouse = selection();
    const bool canSearch = signedIn && care_ && detailLoaded_ && warehouse &&
                           warehouse->visibility == Visibility::Public;
    publicSearchBar_->setVisible(canSearch);
    searchStatus_->setVisible(canSearch);
    submitSearch_->setEnabled(canSearch && !publicSearch_->text().trimmed().isEmpty());
    const bool canWrite = ownsSelection() && !writing_;
    add_->setEnabled(canWrite);
    remove_->setEnabled(canWrite && itemList_->currentRow() >= 0);
    addSuggestion_->setEnabled(canWrite && recommendationList_->currentRow() >= 0);
    recommend_->setEnabled(signedIn && !recommending_);
    query_->setEnabled(signedIn && !recommending_);
    useTarget_->setEnabled(signedIn && !recommending_);
    cancel_->setEnabled(recommending_);
    identity_->setText(signedIn ? QStringLiteral("仓库账号 · %1").arg(user_->username)
                               : QStringLiteral("请使用服务器提供的独立仓库账号。"));
}

void WarehousePage::connectAndLogin()
{
    if (!login_->isEnabled())
        return;
    const auto host = host_->text().trimmed();
    const auto username = username_->text().trimmed();
    if (host.isEmpty() || username.isEmpty() || password_->text().isEmpty()) {
        status_->setText(QStringLiteral("请填写服务器地址、用户名和密码。"));
        return;
    }
    connecting_ = true;
    updateControls();
    status_->setText(QStringLiteral("正在连接服务器…"));
    service_.connectToServer(host, static_cast<quint16>(port_->value()), inSession([this, username](Status status) {
        if (!status.ok()) {
            resetSession();
            displayStatus(status);
            return;
        }
        status_->setText(QStringLiteral("正在登录仓库…"));
        const QString password = password_->text();
        password_->clear();
        service_.login(username, password, inSession([this](Result<UserInfo> result) {
            if (!result.status.ok() || !result.value) {
                resetSession();
                displayStatus(result.status);
                return;
            }
            user_ = *result.value;
            connecting_ = false;
            updateControls();
            if (sessionChanged_)
                sessionChanged_();
            refresh();
        }));
    }));
}

void WarehousePage::resetSession(bool disconnect)
{
    ++sessionEpoch_;
    const bool wasSignedIn = isSignedIn();
    user_.reset();
    writeOutcomeUnknown_ = false;
    connecting_ = false;
    writing_ = false;
    clearRecommendation();
    if (disconnect)
        service_.disconnectFromServer();
    password_->clear();
    query_->clear();
    ++searchGeneration_;
    detailLoaded_ = false;
    activeSearch_.clear();
    publicSearch_->clear();
    searchStatus_->clear();
    publicOnly_->setChecked(false);
    warehouses_.clear();
    warehouseList_->clear();
    items_.clear();
    itemList_->clear();
    updateControls();
    if (wasSignedIn && sessionChanged_)
        sessionChanged_();
}

void WarehousePage::signOut()
{
    if (!user_) {
        resetSession();
        status_->setText(QStringLiteral("已退出仓库。"));
        return;
    }
    resetSession(false);
    connecting_ = true;
    updateControls();
    status_->setText(QStringLiteral("正在退出仓库…"));
    service_.logout(inSession([this](Status status) {
        resetSession();
        status_->setText(status.ok() ? QStringLiteral("已退出仓库。")
                                     : QStringLiteral("已断开本机连接。%1").arg(status.message));
    }));
}

void WarehousePage::displayStatus(const Status& status)
{
    if (!status.ok()) {
        writeOutcomeUnknown_ = status.code == ErrorCode::OutcomeUnknown;
        if (status.code == ErrorCode::NotConnected || status.code == ErrorCode::NotAuthenticated)
            resetSession();
        status_->setText(status.message.isEmpty() ? QStringLiteral("仓库请求失败。") : status.message);
    }
}

void WarehousePage::connectionLost()
{
    if (!isSignedIn())
        return;
    const bool unknown = writeOutcomeUnknown_;
    resetSession(false);
    status_->setText(unknown
        ? QStringLiteral("连接已断开，最后一次写入结果未知。重新登录后请先查询。")
        : QStringLiteral("连接已断开，请重新登录仓库。"));
}

void WarehousePage::refresh()
{
    if (!user_)
        return;
    const auto generation = ++listEpoch_;
    status_->setText(QStringLiteral("正在加载仓库…"));
    auto done = inSession([this, generation](Result<QList<WarehouseSummary>> result) {
        if (generation != listEpoch_)
            return;
        if (!result.status.ok() || !result.value) {
            displayStatus(result.status);
            return;
        }
        // Preserve a selection made while this request was in flight.
        const auto previous = selection();
        const auto previousId = previous ? previous->warehouseId : 0;
        warehouses_ = *result.value;
        {
            const QSignalBlocker block(warehouseList_);
            warehouseList_->clear();
            int selectedRow = -1;
            for (int row = 0; row < warehouses_.size(); ++row) {
                const auto& warehouse = warehouses_.at(row);
                warehouseList_->addItem(QStringLiteral("%1 · %2").arg(warehouse.name,
                    warehouse.visibility == Visibility::Public ? QStringLiteral("公开") : QStringLiteral("私有")));
                if (warehouse.warehouseId == previousId)
                    selectedRow = row;
            }
            warehouseList_->setCurrentRow(selectedRow >= 0 ? selectedRow : (warehouses_.isEmpty() ? -1 : 0));
        }
        const auto current = selection();
        const auto currentId = current ? current->warehouseId : 0;
        if (currentId != previousId)
            clearRecommendation();
        status_->setText(warehouses_.isEmpty() ? QStringLiteral("当前列表没有仓库。") : QString());
        refreshDetail();
    });
    if (publicOnly_->isChecked())
        service_.listPublicWarehouses(done);
    else
        service_.listMyWarehouses(done);
}

void WarehousePage::refreshDetail()
{
    const auto generation = ++detailEpoch_;
    ++searchGeneration_;
    detailLoaded_ = false;
    activeSearch_.clear();
    publicSearch_->clear();
    searchStatus_->clear();
    items_.clear();
    itemList_->clear();
    updateControls();
    const auto warehouse = selection();
    if (!warehouse || !user_)
        return;
    service_.getWarehouse(warehouse->warehouseId, inSession([this, generation](Result<WarehouseDetail> result) {
        if (generation != detailEpoch_)
            return;
        if (!result.status.ok() || !result.value) {
            displayStatus(result.status);
            return;
        }
        const int selectedRow = warehouseList_->currentRow();
        if (selectedRow >= 0 && selectedRow < warehouses_.size())
            warehouses_[selectedRow].visibility = result.value->warehouse.visibility;
        detailLoaded_ = true;
        items_ = result.value->items;
        for (const auto& item : items_) {
            const auto& media = item.media;
            auto* row = new QListWidgetItem(itemLabel(media), itemList_);
            row->setToolTip(QStringLiteral("<b>%1</b><br>导演：%2<br>演员：%3<br>发行日期：%4")
                .arg(media.fileName.toHtmlEscaped(), media.director.toHtmlEscaped(),
                     media.actors.join(QStringLiteral("、")).toHtmlEscaped(),
                     media.releaseDate.toString(Qt::ISODate)));
        }
        if (items_.isEmpty())
            status_->setText(QStringLiteral("这个仓库还没有条目。"));
        updateControls();
    }));
}

void WarehousePage::searchPublicWarehouse()
{
    if (!submitSearch_->isEnabled())
        return;
    activeSearch_ = publicSearch_->text().trimmed();
    applyPublicSearch();
    const auto generation = ++searchGeneration_;
    const auto eventId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    care_->recordBehavior({eventId, care_contract::BehaviorAction::Search, std::nullopt, activeSearch_},
        inSession([this, generation](Status status) {
            if (status.ok() || generation != searchGeneration_)
                return;
            const auto message = status.code == ErrorCode::OutcomeUnknown
                ? QStringLiteral("服务器记录结果未知。")
                : QStringLiteral("搜索历史：%1").arg(status.message.isEmpty() ? QStringLiteral("记录失败。") : status.message);
            searchStatus_->setText(searchStatus_->text() + QStringLiteral(" ") + message);
        }));
}

void WarehousePage::applyPublicSearch()
{
    itemList_->setCurrentRow(-1);
    int shown = 0;
    for (int row = 0; row < items_.size(); ++row) {
        const auto& media = items_.at(row).media;
        const auto matches = [this](const QString& value) {
            return value.contains(activeSearch_, Qt::CaseInsensitive);
        };
        const bool visible = activeSearch_.isEmpty() || matches(media.fileName) ||
            matches(media.category) || matches(media.director) ||
            std::any_of(media.actors.cbegin(), media.actors.cend(), matches);
        itemList_->item(row)->setHidden(!visible);
        if (visible)
            ++shown;
    }
    searchStatus_->setText(activeSearch_.isEmpty() ? QString() :
        QStringLiteral("当前公开仓库：找到 %1 项。").arg(shown));
}

void WarehousePage::recordMediaView(const care_contract::WarehouseItemRef& source,
                                    care_contract::Callback<care_contract::ResourceSummary> opened)
{
    if (!care_ || !user_) {
        if (opened) opened({{ErrorCode::NotAuthenticated, QStringLiteral("请重新登录服务器账号。")}, {}});
        return;
    }
    const auto detail = detailEpoch_;
    const auto eventId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    care_->getResource(source, inSession([this, detail, eventId, opened = std::move(opened)](Result<care_contract::ResourceSummary> result) {
        if (!result.status.ok() || !result.value) {
            if (result.status.ok())
                result.status = {ErrorCode::ProtocolError, QStringLiteral("无法识别所选媒体。")};
            if (opened)
                opened(std::move(result));
            else if (detail == detailEpoch_)
                displayStatus(result.status);
            return;
        }
        care_->recordBehavior({eventId, care_contract::BehaviorAction::View, result.value->resource, {}},
            inSession([this, detail](Status result) {
                if (!result.ok() && detail == detailEpoch_)
                    displayStatus(result);
            }));
        if (opened)
            opened(std::move(result));
    }));
}

void WarehousePage::createWarehouse()
{
    if (!create_->isEnabled())
        return;
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("新建仓库"));
    QFormLayout form(&dialog);
    QLineEdit name;
    name.setObjectName(QStringLiteral("newWarehouseName"));
    name.setMaxLength(100);
    QCheckBox shared(QStringLiteral("允许其他账号查看"));
    QPushButton confirm(QStringLiteral("创建"));
    form.addRow(QStringLiteral("仓库名称"), &name);
    form.addRow(&shared);
    form.addRow(&confirm);
    connect(&name, &QLineEdit::textChanged, &confirm, [&confirm](const QString& text) {
        confirm.setEnabled(!text.trimmed().isEmpty());
    });
    confirm.setEnabled(false);
    connect(&confirm, &QPushButton::clicked, &dialog, &QDialog::accept);
    if (dialog.exec() != QDialog::Accepted || !user_)
        return;
    writing_ = true;
    updateControls();
    service_.createWarehouse(name.text().trimmed(), shared.isChecked() ? Visibility::Public : Visibility::Private,
        inSession([this](Result<WarehouseSummary> result) {
            writing_ = false;
            updateControls();
            if (!result.status.ok()) {
                displayStatus(result.status);
                return;
            }
            if (publicOnly_->isChecked())
                publicOnly_->setChecked(false);
            else
                refresh();
        }));
}

void WarehousePage::addLocalMedia()
{
    if (!add_->isEnabled())
        return;
    const auto local = localRecords_();
    if (local.isEmpty()) {
        status_->setText(QStringLiteral("本地媒体库没有可添加的作品。"));
        return;
    }
    QStringList choices;
    for (const auto& record : local)
        choices << QStringLiteral("%1 · %2").arg(itemLabel(record), record.id);
    bool accepted = false;
    const auto choice = QInputDialog::getItem(this, QStringLiteral("添加本地作品"), QStringLiteral("选择媒体"),
                                             choices, 0, false, &accepted);
    const auto row = choices.indexOf(choice);
    if (accepted && row >= 0)
        addMedia(local.at(row));
}

void WarehousePage::addMedia(const MediaRecord& record)
{
    const auto warehouse = selection();
    if (!warehouse || !ownsSelection() || writing_)
        return;
    writing_ = true;
    updateControls();
    service_.addItem(warehouse->warehouseId, record, inSession([this](Result<WarehouseItem> result) {
        writing_ = false;
        updateControls();
        if (result.status.ok())
            refreshDetail();
        else
            displayStatus(result.status);
    }));
}

void WarehousePage::removeItem()
{
    const auto warehouse = selection();
    const auto row = itemList_->currentRow();
    if (!warehouse || !remove_->isEnabled() || row < 0 || row >= items_.size())
        return;
    const auto item = items_.at(row);
    const auto epoch = sessionEpoch_;
    if (QMessageBox::question(this, QStringLiteral("删除条目"),
        QStringLiteral("确认从仓库删除所选条目？"), QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes || epoch != sessionEpoch_)
        return;
    writing_ = true;
    updateControls();
    service_.removeItem(warehouse->warehouseId, item.itemId, inSession([this](Status status) {
        writing_ = false;
        updateControls();
        if (status.ok())
            refreshDetail();
        else
            displayStatus(status);
    }));
}

void WarehousePage::recommend()
{
    if (!recommend_->isEnabled())
        return;
    const auto query = query_->text().trimmed();
    if (query.isEmpty()) {
        recommendationStatus_->setText(QStringLiteral("请输入推荐需求。"));
        return;
    }
    clearRecommendation();
    const auto warehouse = selection();
    const auto target = useTarget_->isChecked() && warehouse
        ? std::optional<qint64>(warehouse->warehouseId) : std::nullopt;
    const auto generation = recommendationEpoch_;
    recommending_ = true;
    updateControls();
    recommendationStatus_->setText(QStringLiteral("服务器正在生成推荐…"));
    const auto handle = service_.recommendResources({query, target, 5},
        inSession([this, generation](Result<WarehouseRecommendationResult> result) {
            if (generation != recommendationEpoch_)
                return;
            recommending_ = false;
            handle_.clear();
            if (!result.status.ok() || !result.value) {
                if (result.status.code == ErrorCode::NotConnected || result.status.code == ErrorCode::NotAuthenticated)
                    displayStatus(result.status);
                else
                    recommendationStatus_->setText(result.status.message.isEmpty()
                        ? QStringLiteral("推荐请求失败，请重试。") : result.status.message);
                updateControls();
                return;
            }
            const auto& value = *result.value;
            suggestions_ = value.items;
            for (const auto& suggestion : suggestions_)
                recommendationList_->addItem(itemLabel(suggestion.media));
            QString message = value.message;
            if (message.isEmpty())
                message = suggestions_.isEmpty() ? QStringLiteral("没有符合条件的资源。")
                                                 : QStringLiteral("找到 %1 条推荐。").arg(suggestions_.size());
            recommendationStatus_->setText(value.fallback ? QStringLiteral("备用推荐 · %1").arg(message) : message);
            updateControls();
        }));
    if (recommending_ && generation == recommendationEpoch_)
        handle_ = handle;
}

void WarehousePage::clearRecommendation()
{
    ++recommendationEpoch_;
    recommending_ = false;
    const auto handle = std::exchange(handle_, {});
    suggestions_.clear();
    recommendationList_->clear();
    reason_->clear();
    recommendationStatus_->clear();
    if (!handle.isEmpty())
        service_.cancelRecommendation(handle);
    updateControls();
}

void WarehousePage::suspend()
{
    clearRecommendation();
}

void WarehousePage::addSuggestion()
{
    const auto row = recommendationList_->currentRow();
    const auto warehouse = selection();
    if (!warehouse || !addSuggestion_->isEnabled() || row < 0 || row >= suggestions_.size())
        return;
    const auto record = suggestions_.at(row).media;
    const auto epoch = sessionEpoch_;
    const auto detailGeneration = detailEpoch_;
    if (QMessageBox::question(this, QStringLiteral("确认入库"),
        QStringLiteral("将所选推荐作品加入当前仓库？"), QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes || epoch != sessionEpoch_)
        return;
    const auto current = selection();
    if (!current || current->warehouseId != warehouse->warehouseId || detailGeneration != detailEpoch_) {
        status_->setText(QStringLiteral("仓库已变化，请重新确认入库。"));
        return;
    }
    addMedia(record);
}
