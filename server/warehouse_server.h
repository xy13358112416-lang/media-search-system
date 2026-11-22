// >>> MAIN-DEFENSE | 主逻辑头文件导读
// 在线业务总路由：连接与会话、账号验证、仓库资源权限、行为持久化及足迹/规则推荐快照。所有用户身份以当前连接session为准。
// 阅读顺序：输入数据结构 → 返回状态/可选数据 → 方法声明 → 私有成员。引用/指针保存依赖，必须确认由谁拥有及何时释放。
// <<< MAIN-DEFENSE
#pragma once

#include "warehouse_contract.h"
#include "warehouse_protocol.h"
#include "llm_recommendation.h"
#include "llm_trend_summary.h"
#include "llm_scene.h"
#include <QHash>
#include <QSet>
#include "behavior_contract.h"
#include "care_person_snapshot.h"
#include <QSqlDatabase>
#include <QTcpServer>
#include <memory>
#include <utility>

class QTcpSocket;
class QNetworkAccessManager;
namespace person_store { class SqliteStore; }

// >>> MAIN-DEFENSE | 接口结构｜StoredBehavior
// 这是数据包：检查字段含义、默认值和optional缺失状态。
// <<< MAIN-DEFENSE
struct StoredBehavior {
    care_contract::BehaviorRecord record;
    std::optional<MediaRecord> mediaSnapshot; // Historical view metadata, not current access permission.
    bool publicEligible = false; // Eligibility at collection; current source visibility must also be checked.
    QList<care_contract::PersonSummary> peopleSnapshot; // Captured credits; never reconstructed from today's names.
};

// >>> MAIN-DEFENSE | 接口结构｜WarehouseServer
// 这是服务对象：public为调用入口，private保存内部状态；Qt或其他模块通过接口调用，业务权限在实现中校验。
// <<< MAIN-DEFENSE
class WarehouseServer final : public QTcpServer {
public:
    // >>> MAIN-DEFENSE | 方法声明｜WarehouseServer
    // 本行约定调用参数和返回类型；具体分块处理在对应cpp，调用者须遵守已有注释中的权限与生命周期约定。
    // <<< MAIN-DEFENSE
    explicit WarehouseServer(llm_recommendation::Config config = llm_recommendation::loadConfig());
    // >>> MAIN-DEFENSE | 方法声明｜WarehouseServer
    // 本行约定调用参数和返回类型；具体分块处理在对应cpp，调用者须遵守已有注释中的权限与生命周期约定。
    // <<< MAIN-DEFENSE
    ~WarehouseServer() override;
    // >>> MAIN-DEFENSE | 方法声明｜openDatabase
    // 打开在线仓库SQLite并初始化/核对schema，随后人物迁移复用此连接。
    // <<< MAIN-DEFENSE
    bool openDatabase(const QString& path, QString& error);
    bool addUser(const QString& username, const QString& password, QString& error, bool administrator = false, qint64 actorId = 0);
    // >>> MAIN-DEFENSE | 方法声明｜addUser
    // 创建在线network_users账号并哈希密码；在线仓库权限按所有者，不使用单机admin角色替代。
    // <<< MAIN-DEFENSE
    // >>> MAIN-DEFENSE | 方法声明｜readableCandidates
    // 只整理当前用户可读的仓库条目，供推荐使用，不把别人私有数据传给模型。
    // <<< MAIN-DEFENSE
    std::optional<QList<warehouse_contract::WarehouseDetail>> readableCandidates(qint64 userId);
    // Internal server API: caller supplies the authenticated user, never a network payload ID.
    // >>> MAIN-DEFENSE | 方法声明｜queryBehavior
    // 按用户与时间窗读服务器行为记录，保存读取失败状态和历史快照。
    // <<< MAIN-DEFENSE
    std::optional<QList<StoredBehavior>> queryBehavior(qint64 userId, care_contract::TimeRange range);
    // Server-owned store adapter; called on this object's thread AFTER authorization.
    // Loader must not infer identity from client media IDs/names or broaden the supplied sources.
    using PersonContextLoader = std::function<care_contract::Result<care_algorithms::PersonContext>(
        qint64 userId, const QList<care_contract::ResourceRef>& authorizedResources)>;
    void setPersonContextLoader(PersonContextLoader loader) { personContextLoader_ = std::move(loader); }

private:
    // >>> MAIN-DEFENSE | 接口结构｜Session
    // 这是数据包：检查字段含义、默认值和optional缺失状态。
    // <<< MAIN-DEFENSE
    struct Session {
        // >>> MAIN-DEFENSE | 接口结构｜Pending
        // 这是数据包：检查字段含义、默认值和optional缺失状态。
        // <<< MAIN-DEFENSE
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
        // >>> MAIN-DEFENSE | 接口结构｜SummaryPending
        // 这是数据包：检查字段含义、默认值和optional缺失状态。
        // <<< MAIN-DEFENSE
        struct SummaryPending { quint64 modelId = 0; QString token; qint64 userId = 0; };
        QHash<QString, SummaryPending> summaries;
        QHash<QString, SummaryPending> scenes;
        QString personLookup;
    };
    // >>> MAIN-DEFENSE | 方法声明｜dispatch
    // 按请求type分发同步业务，使用当前session校验身份与权限后访问数据库或算法。
    // <<< MAIN-DEFENSE
    QJsonObject dispatch(Session& session, const QJsonObject& request);
    QJsonObject dispatchAdmin(Session& session, const QJsonObject& request);
    void revokeUserSessions(qint64 userId);
    QHash<QTcpSocket*, std::shared_ptr<Session>> sessions_;
    std::optional<care_contract::Result<QJsonObject>> dispatchPersonQuery(
        qint64 userId, const QString& type, const QJsonObject& payload);
    care_contract::Result<care_algorithms::CatalogSnapshot> readablePersonCatalog(
        qint64 userId, const QString& personId, bool includeAliases);
    care_contract::Result<care_algorithms::PersonContext> loadPersonContext(
        qint64 userId, const QList<care_contract::ResourceSummary>& authorizedWorks);
    care_contract::Result<care_algorithms::CatalogSnapshot> personCatalog(
        qint64 userId, const QList<care_contract::ResourceSummary>& authorizedWorks);
    // >>> MAIN-DEFENSE | 方法声明｜readRequests
    // 本行约定调用参数和返回类型；具体分块处理在对应cpp，调用者须遵守已有注释中的权限与生命周期约定。
    // <<< MAIN-DEFENSE
    void readRequests(QTcpSocket* socket, const std::shared_ptr<Session>& session);
    // >>> MAIN-DEFENSE | 方法声明｜sendResponse
    // 将业务结果编码为协议帧并写回当前socket。
    // <<< MAIN-DEFENSE
    bool sendResponse(QTcpSocket* socket, const QJsonObject& response);
    // >>> MAIN-DEFENSE | 方法声明｜startRecommendation
    // 本行约定调用参数和返回类型；具体分块处理在对应cpp，调用者须遵守已有注释中的权限与生命周期约定。
    // <<< MAIN-DEFENSE
    void startRecommendation(QTcpSocket* socket, const std::shared_ptr<Session>& session, const QJsonObject& request);
    // >>> MAIN-DEFENSE | 方法声明｜cancelPending
    // 本行约定调用参数和返回类型；具体分块处理在对应cpp，调用者须遵守已有注释中的权限与生命周期约定。
    // <<< MAIN-DEFENSE
    void cancelPending(Session& session, QTcpSocket* socket, bool notify);
    // >>> MAIN-DEFENSE | 方法声明｜parseTrendingRequest
    // 本行约定调用参数和返回类型；具体分块处理在对应cpp，调用者须遵守已有注释中的权限与生命周期约定。
    // <<< MAIN-DEFENSE
    static care_contract::Status parseTrendingRequest(const QJsonObject& payload, care_contract::TrendingRequest& input);
    // >>> MAIN-DEFENSE | 方法声明｜trendingJson
    // 本行约定调用参数和返回类型；具体分块处理在对应cpp，调用者须遵守已有注释中的权限与生命周期约定。
    // <<< MAIN-DEFENSE
    static QJsonObject trendingJson(const care_contract::TrendingResult& result);
    // >>> MAIN-DEFENSE | 方法声明｜currentTrending
    // 本行约定调用参数和返回类型；具体分块处理在对应cpp，调用者须遵守已有注释中的权限与生命周期约定。
    // <<< MAIN-DEFENSE
    care_contract::Result<care_contract::TrendingResult> currentTrending(const care_contract::TrendingRequest& input);
    // >>> MAIN-DEFENSE | 方法声明｜startTrendingSummary
    // 本行约定调用参数和返回类型；具体分块处理在对应cpp，调用者须遵守已有注释中的权限与生命周期约定。
    // <<< MAIN-DEFENSE
    void startTrendingSummary(QTcpSocket* socket, const std::shared_ptr<Session>& session, const QJsonObject& request);
    // >>> MAIN-DEFENSE | 方法声明｜startSceneRecommendation
    // 本行约定调用参数和返回类型；具体分块处理在对应cpp，调用者须遵守已有注释中的权限与生命周期约定。
    // <<< MAIN-DEFENSE
    void startSceneRecommendation(QTcpSocket* socket, const std::shared_ptr<Session>& session, const QJsonObject& request);
    // >>> MAIN-DEFENSE | 方法声明｜startPersonLookup
    // 发起人物资料异步查询，绑定当前用户/连接，结束后不能向失效会话交付。
    // <<< MAIN-DEFENSE
    void startPersonLookup(QTcpSocket* socket, const std::shared_ptr<Session>& session, const QJsonObject& request);
    QNetworkAccessManager* personLookupNetwork_;
    int personLookupsInFlight_ = 0;
    llm_recommendation::Client recommender_;
    llm_trend_summary::Client trendSummarizer_;
    llm_scene::Client sceneRecommender_;
    QString dbName_;
    QSqlDatabase db_;
    std::unique_ptr<person_store::SqliteStore> personStore_;
    PersonContextLoader personContextLoader_;
};
