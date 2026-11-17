#pragma once

#include "warehouse_client.h"
#include "care_contract.h"

// Shares the WarehouseClient connection, session and pending request table.
// Person operations use the same authenticated connection and wait for server replies.
class OnlineCareClient final : public WarehouseClient, public care_contract::CareService {
public:
    using WarehouseClient::WarehouseClient;
    bool isAuthenticated() const
    {
        return socket_.state() == QAbstractSocket::ConnectedState && !token_.isEmpty();
    }
    QMetaObject::Connection onDisconnected(QObject* context, std::function<void()> callback)
    {
        return connect(&socket_, &QTcpSocket::disconnected, context, std::move(callback));
    }
    void recordBehavior(const care_contract::BehaviorEvent&, care_contract::StatusCallback) override;
    void getParticipationSettings(care_contract::Callback<care_contract::ParticipationSettings>) override;
    void setParticipationSettings(const care_contract::ParticipationSettings&, care_contract::StatusCallback) override;
    void clearHistory(care_contract::StatusCallback) override;
    void getResource(const care_contract::WarehouseItemRef&, care_contract::Callback<care_contract::ResourceSummary>) override;
    void searchPeople(const care_contract::PersonSearchRequest&, care_contract::Callback<care_contract::Page<care_contract::PersonSummary>>) override;
    void searchPersonWeb(const QString&, care_contract::Callback<QList<care_contract::PersonWebSource>>) override;
    void listWorkFeedback(const care_contract::PageRequest&, care_contract::Callback<care_contract::Page<care_contract::WorkFeedback>>) override;
    void setWorkFeedback(const care_contract::WorkFeedbackRequest&, care_contract::StatusCallback) override;
    void listFollows(const care_contract::PageRequest&, care_contract::Callback<care_contract::Page<care_contract::PersonSummary>>) override;
    void setFollow(const QString&, bool, care_contract::StatusCallback) override;
    void getPersonWorks(const QString&, const care_contract::PageRequest&, care_contract::Callback<care_contract::Page<care_contract::ResourceSummary>>) override;
    void getPersonUpdates(const care_contract::PersonUpdatesRequest&, care_contract::Callback<care_contract::Page<care_contract::PersonUpdate>>) override;
    void getTrending(const care_contract::TrendingRequest&, care_contract::Callback<care_contract::TrendingResult>) override;
    void getFootprint(const care_contract::FootprintRequest&, care_contract::Callback<care_contract::FootprintReport>) override;
    warehouse_contract::RecommendationHandle recommendTemporal(const care_contract::TemporalRecommendationRequest&, care_contract::Callback<care_contract::TemporalRecommendationResult>) override;
    bool cancelRecommendation(const warehouse_contract::RecommendationHandle&) override;

private:
    template<class Item>
    void requestPersonPage(const QString& type, QJsonObject payload, const care_contract::PageRequest& page,
                           care_contract::Callback<care_contract::Page<Item>> done);
};
