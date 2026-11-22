// >>> MAIN-DEFENSE | 主逻辑头文件导读
// 人物SQLite仓储：当前用户可见人物上下文、本人关注、资讯缓存与刷新代次。关注不扩大资源权限，取关仍可处理已失去来源的旧关注。
// 阅读顺序：输入数据结构 → 返回状态/可选数据 → 方法声明 → 私有成员。引用/指针保存依赖，必须确认由谁拥有及何时释放。
// <<< MAIN-DEFENSE
#pragma once

#include "person_store.h"
#include <QSqlDatabase>

namespace person_store {
// Uses the server's existing SQLite connection on its owning thread.
// >>> MAIN-DEFENSE | 接口结构｜SqliteStore
// 这是服务对象：public为调用入口，private保存内部状态；Qt或其他模块通过接口调用，业务权限在实现中校验。
// <<< MAIN-DEFENSE
class SqliteStore final : public Store {
public:
    // >>> MAIN-DEFENSE | 方法声明｜SqliteStore
    // 本行约定调用参数和返回类型；具体分块处理在对应cpp，调用者须遵守已有注释中的权限与生命周期约定。
    // <<< MAIN-DEFENSE
    explicit SqliteStore(QSqlDatabase database);
    care_contract::Result<care_algorithms::PersonContext> contextFor(
        qint64 userId, const QList<care_contract::ResourceRef>& resources) override;
    care_contract::Result<care_contract::Page<care_contract::PersonSummary>> followsFor(
        qint64 userId, const care_contract::PageRequest& page) override;
    // >>> MAIN-DEFENSE | 方法声明｜setFollow
    // 关注先核验人物可见性；取关按本人user_id+person_id删除，撤权后的旧关注仍能取消。
    // <<< MAIN-DEFENSE
    care_contract::Status setFollow(qint64 userId, const QString& personId, bool followed) override;
    // >>> MAIN-DEFENSE | 方法声明｜updatesFor
    // 先核验人物读权限，再读来源状态和文章缓存；来源过期与文章过期都必须反映。
    // <<< MAIN-DEFENSE
    care_contract::Result<QList<care_contract::PersonUpdate>> updatesFor(qint64 userId, const QString& personId) override;
    care_contract::Result<qint64> beginUpdatesRefresh(
        const QString& personId, const QUrl& feedUrl) override;
    care_contract::Status failUpdatesRefresh(
        const QString& personId, const QUrl& feedUrl, qint64 generation) override;
    care_contract::Status mergeUpdates(const QString& personId, const QUrl& feedUrl,
        qint64 generation, qint64 completedAtUtc,
        const QList<care_contract::PersonUpdate>& updates) override;
private:
    QSqlDatabase db_;
};
}
