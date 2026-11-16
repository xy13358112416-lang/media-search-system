#pragma once

#include "warehouse_contract.h"
#include "warehouse_protocol.h"
#include "llm_recommendation.h"
#include "llm_trend_summary.h"
#include <QHash>
#include <QSet>
#include "behavior_contract.h"
#include "care_person_snapshot.h"
#include <QSqlDatabase>
#include <QTcpServer>
#include <memory>
#include <utility>

class QTcpSocket;
namespace person_store { class SqliteStore; }

struct StoredBehavior {
    care_contract::BehaviorRecord record;
    std::optional<MediaRecord> mediaSnapshot; // Historical view metadata, not current access permission.
    bool publicEligible = false; // Eligibility at collection; current source visibility must also be checked.
    QList<care_contract::PersonSummary> peopleSnapshot; // Captured credits; never reconstructed from today's names.
};

class WarehouseServer final : public QTcpServer {
public:
    explicit WarehouseServer(llm_recommendation::Config config = llm_recommendation::loadConfig());
    ~WarehouseServer() override;
    bool openDatabase(const QString& path, QString& error);
    bool addUser(const QString& username, const QString& password, QString& error);
    std::optional<QList<warehouse_contract::WarehouseDetail>> readableCandidates(qint64 userId);
    // Internal server API: caller supplies the authenticated user, never a network payload ID.
    std::optional<QList<StoredBehavior>> queryBehavior(qint64 userId, care_contract::TimeRange range);
    // Server-owned store adapter; called on this object's thread AFTER authorization.
    // Loader must not infer identity from client media IDs/names or broaden the supplied sources.
    using PersonContextLoader = std::function<care_contract::Result<care_algorithms::PersonContext>(
        qint64 userId, const QList<care_contract::ResourceRef>& authorizedResources)>;
    void setPersonContextLoader(PersonContextLoader loader) { personContextLoader_ = std::move(loader); }

private:
    struct Session {
        struct Pending {
            quint64 modelId = 0;
            QString token;
            qint64 userId = 0;
            std::optional<qint64> target;
            QSet<QString> candidateKeys;
        };
        warehouse_protocol::FrameDecoder decoder;
        QString token;
        qint64 userId = 0;
        QHash<QString, Pending> pending;
        struct SummaryPending { quint64 modelId = 0; QString token; qint64 userId = 0; };
        QHash<QString, SummaryPending> summaries;
    };
    QJsonObject dispatch(Session& session, const QJsonObject& request);
    care_contract::Result<care_algorithms::PersonContext> loadPersonContext(
        qint64 userId, const QList<care_contract::ResourceSummary>& authorizedWorks);
    care_contract::Result<care_algorithms::CatalogSnapshot> personCatalog(
        qint64 userId, const QList<care_contract::ResourceSummary>& authorizedWorks);
    void readRequests(QTcpSocket* socket, const std::shared_ptr<Session>& session);
    bool sendResponse(QTcpSocket* socket, const QJsonObject& response);
    void startRecommendation(QTcpSocket* socket, const std::shared_ptr<Session>& session, const QJsonObject& request);
    void cancelPending(Session& session, QTcpSocket* socket, bool notify);
    static care_contract::Status parseTrendingRequest(const QJsonObject& payload, care_contract::TrendingRequest& input);
    static QJsonObject trendingJson(const care_contract::TrendingResult& result);
    care_contract::Result<care_contract::TrendingResult> currentTrending(const care_contract::TrendingRequest& input);
    void startTrendingSummary(QTcpSocket* socket, const std::shared_ptr<Session>& session, const QJsonObject& request);
    llm_recommendation::Client recommender_;
    llm_trend_summary::Client trendSummarizer_;
    QString dbName_;
    QSqlDatabase db_;
    std::unique_ptr<person_store::SqliteStore> personStore_;
    PersonContextLoader personContextLoader_;
};
