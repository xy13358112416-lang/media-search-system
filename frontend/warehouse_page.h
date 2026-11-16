#pragma once

#include "care_contract.h"
#include <QPointer>
#include <QWidget>
#include <utility>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;

class WarehousePage final : public QWidget {
public:
    WarehousePage(warehouse_contract::WarehouseService& service,
                  std::function<QList<MediaRecord>()> localRecords, QWidget* parent = nullptr);
    ~WarehousePage() override;
    void suspend();
    void connectionLost();
    bool isSignedIn() const { return user_.has_value(); }
    void setSessionChangedCallback(std::function<void()> callback) { sessionChanged_ = std::move(callback); }

private:
    std::optional<warehouse_contract::WarehouseSummary> selection() const;
    bool ownsSelection() const;
    void updateControls();
    void connectAndLogin();
    void signOut();
    void resetSession(bool disconnect = true);
    void refresh();
    void refreshDetail();
    void recordMediaView(const care_contract::WarehouseItemRef& source);
    void createWarehouse();
    void addLocalMedia();
    void removeItem();
    void recommend();
    void clearRecommendation();
    void addSuggestion();
    void addMedia(const MediaRecord& record);
    void displayStatus(const warehouse_contract::Status& status);

    // Requests may complete synchronously or after a logout or page destruction.
    template<typename Callback>
    auto inSession(Callback callback)
    {
        return [page = QPointer<WarehousePage>(this), epoch = sessionEpoch_, callback](auto result) {
            if (page && page->sessionEpoch_ == epoch)
                callback(std::move(result));
        };
    }

    warehouse_contract::WarehouseService& service_;
    care_contract::CareService* care_;
    std::function<void()> sessionChanged_;
    std::function<QList<MediaRecord>()> localRecords_;
    std::optional<warehouse_contract::UserInfo> user_;
    QList<warehouse_contract::WarehouseSummary> warehouses_;
    QList<warehouse_contract::WarehouseItem> items_;
    QList<warehouse_contract::WarehouseRecommendationItem> suggestions_;
    warehouse_contract::RecommendationHandle handle_;
    quint64 sessionEpoch_ = 0;
    quint64 listEpoch_ = 0;
    quint64 detailEpoch_ = 0;
    quint64 recommendationEpoch_ = 0;
    bool connecting_ = false;
    bool recommending_ = false;
    bool writing_ = false;
    bool writeOutcomeUnknown_ = false;

    QLineEdit* host_;
    QSpinBox* port_;
    QLineEdit* username_;
    QLineEdit* password_;
    QLabel* identity_;
    QLabel* status_;
    QPushButton* login_;
    QPushButton* logout_;
    QPushButton* refresh_;
    QPushButton* create_;
    QPushButton* add_;
    QPushButton* remove_;
    QPushButton* recommend_;
    QPushButton* cancel_;
    QPushButton* addSuggestion_;
    QListWidget* warehouseList_;
    QListWidget* itemList_;
    QListWidget* recommendationList_;
    QCheckBox* publicOnly_;
    QCheckBox* useTarget_;
    QLineEdit* query_;
    QLabel* reason_;
    QLabel* recommendationStatus_ = nullptr;
};
