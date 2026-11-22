// >>> MAIN-DEFENSE | 主逻辑头文件导读
// 共享接口/数据契约：上层通过这里的类型和方法与主逻辑对接。声明不等于执行，实际实现见对应cpp。
// 阅读顺序：输入数据结构 → 返回状态/可选数据 → 方法声明 → 私有成员。引用/指针保存依赖，必须确认由谁拥有及何时释放。
// <<< MAIN-DEFENSE
#pragma once

#include "warehouse_client.h"
#include "care_contract.h"

// Shares the WarehouseClient connection, session and pending request table.
// Person operations use the same authenticated connection and wait for server replies.
// >>> MAIN-DEFENSE | 接口结构｜OnlineCareClient
// 这是服务对象：public为调用入口，private保存内部状态；Qt或其他模块通过接口调用，业务权限在实现中校验。
// <<< MAIN-DEFENSE
class OnlineCareClient final : public WarehouseClient, public care_contract::CareService {
public:
    using WarehouseClient::WarehouseClient;
    bool isAuthenticated() const
    {
        return socket_.state() == QAbstractSocket::ConnectedState && !token_.isEmpty();
    }
    QMetaObject::Connection onDisconnected(QObject* context, std::function<void()> callback)
    {
        // >>> MAIN-DEFENSE | 方法声明｜connect
        // 本行约定调用参数和返回类型；具体分块处理在对应cpp，调用者须遵守已有注释中的权限与生命周期约定。
        // <<< MAIN-DEFENSE
        return connect(&socket_, &QTcpSocket::disconnected, context, std::move(callback));
    }
    // >>> MAIN-DEFENSE | 方法声明｜recordBehavior
    // 本行约定调用参数和返回类型；具体分块处理在对应cpp，调用者须遵守已有注释中的权限与生命周期约定。
    // <<< MAIN-DEFENSE
    void recordBehavior(const care_contract::BehaviorEvent&, care_contract::StatusCallback) override;
    // >>> MAIN-DEFENSE | 方法声明｜getParticipationSettings
    // 本行约定调用参数和返回类型；具体分块处理在对应cpp，调用者须遵守已有注释中的权限与生命周期约定。
    // <<< MAIN-DEFENSE
    void getParticipationSettings(care_contract::Callback<care_contract::ParticipationSettings>) override;
    // >>> MAIN-DEFENSE | 方法声明｜setParticipationSettings
    // 本行约定调用参数和返回类型；具体分块处理在对应cpp，调用者须遵守已有注释中的权限与生命周期约定。
    // <<< MAIN-DEFENSE
    void setParticipationSettings(const care_contract::ParticipationSettings&, care_contract::StatusCallback) override;
    // >>> MAIN-DEFENSE | 方法声明｜clearHistory
    // 本行约定调用参数和返回类型；具体分块处理在对应cpp，调用者须遵守已有注释中的权限与生命周期约定。
    // <<< MAIN-DEFENSE
    void clearHistory(care_contract::StatusCallback) override;
    // >>> MAIN-DEFENSE | 方法声明｜getResource
    // 本行约定调用参数和返回类型；具体分块处理在对应cpp，调用者须遵守已有注释中的权限与生命周期约定。
    // <<< MAIN-DEFENSE
    void getResource(const care_contract::WarehouseItemRef&, care_contract::Callback<care_contract::ResourceSummary>) override;
    // >>> MAIN-DEFENSE | 方法声明｜searchPeople
    // 本行约定调用参数和返回类型；具体分块处理在对应cpp，调用者须遵守已有注释中的权限与生命周期约定。
    // <<< MAIN-DEFENSE
    void searchPeople(const care_contract::PersonSearchRequest&, care_contract::Callback<care_contract::Page<care_contract::PersonSummary>>) override;
    // >>> MAIN-DEFENSE | 方法声明｜searchPersonWeb
    // 本行约定调用参数和返回类型；具体分块处理在对应cpp，调用者须遵守已有注释中的权限与生命周期约定。
    // <<< MAIN-DEFENSE
    void searchPersonWeb(const QString&, care_contract::Callback<QList<care_contract::PersonWebSource>>) override;
    // >>> MAIN-DEFENSE | 方法声明｜listWorkFeedback
    // 本行约定调用参数和返回类型；具体分块处理在对应cpp，调用者须遵守已有注释中的权限与生命周期约定。
    // <<< MAIN-DEFENSE
    void listWorkFeedback(const care_contract::PageRequest&, care_contract::Callback<care_contract::Page<care_contract::WorkFeedback>>) override;
    // >>> MAIN-DEFENSE | 方法声明｜setWorkFeedback
    // 本行约定调用参数和返回类型；具体分块处理在对应cpp，调用者须遵守已有注释中的权限与生命周期约定。
    // <<< MAIN-DEFENSE
    void setWorkFeedback(const care_contract::WorkFeedbackRequest&, care_contract::StatusCallback) override;
    // >>> MAIN-DEFENSE | 方法声明｜listFollows
    // 本行约定调用参数和返回类型；具体分块处理在对应cpp，调用者须遵守已有注释中的权限与生命周期约定。
    // <<< MAIN-DEFENSE
    void listFollows(const care_contract::PageRequest&, care_contract::Callback<care_contract::Page<care_contract::PersonSummary>>) override;
    // >>> MAIN-DEFENSE | 方法声明｜setFollow
    // 关注先核验人物可见性；取关按本人user_id+person_id删除，撤权后的旧关注仍能取消。
    // <<< MAIN-DEFENSE
    void setFollow(const QString&, bool, care_contract::StatusCallback) override;
    // >>> MAIN-DEFENSE | 方法声明｜getPersonWorks
    // 本行约定调用参数和返回类型；具体分块处理在对应cpp，调用者须遵守已有注释中的权限与生命周期约定。
    // <<< MAIN-DEFENSE
    void getPersonWorks(const QString&, const care_contract::PageRequest&, care_contract::Callback<care_contract::Page<care_contract::ResourceSummary>>) override;
    // >>> MAIN-DEFENSE | 方法声明｜getPersonUpdates
    // 本行约定调用参数和返回类型；具体分块处理在对应cpp，调用者须遵守已有注释中的权限与生命周期约定。
    // <<< MAIN-DEFENSE
    void getPersonUpdates(const care_contract::PersonUpdatesRequest&, care_contract::Callback<care_contract::Page<care_contract::PersonUpdate>>) override;
    // >>> MAIN-DEFENSE | 方法声明｜getTrending
    // 本行约定调用参数和返回类型；具体分块处理在对应cpp，调用者须遵守已有注释中的权限与生命周期约定。
    // <<< MAIN-DEFENSE
    void getTrending(const care_contract::TrendingRequest&, care_contract::Callback<care_contract::TrendingResult>) override;
    // >>> MAIN-DEFENSE | 方法声明｜getFootprint
    // 本行约定调用参数和返回类型；具体分块处理在对应cpp，调用者须遵守已有注释中的权限与生命周期约定。
    // <<< MAIN-DEFENSE
    void getFootprint(const care_contract::FootprintRequest&, care_contract::Callback<care_contract::FootprintReport>) override;
    // >>> MAIN-DEFENSE | 方法声明｜recommendTemporal
    // 本行约定调用参数和返回类型；具体分块处理在对应cpp，调用者须遵守已有注释中的权限与生命周期约定。
    // <<< MAIN-DEFENSE
    warehouse_contract::RecommendationHandle recommendTemporal(const care_contract::TemporalRecommendationRequest&, care_contract::Callback<care_contract::TemporalRecommendationResult>) override;
    // >>> MAIN-DEFENSE | 方法声明｜cancelRecommendation
    // 通过请求句柄取消模型类任务并抑制旧结果交付。
    // <<< MAIN-DEFENSE
    bool cancelRecommendation(const warehouse_contract::RecommendationHandle&) override;

private:
    template<class Item>
    void requestPersonPage(const QString& type, QJsonObject payload, const care_contract::PageRequest& page,
                           care_contract::Callback<care_contract::Page<Item>> done);
};
