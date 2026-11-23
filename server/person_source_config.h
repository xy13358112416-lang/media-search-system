// >>> MAIN-DEFENSE | 主逻辑头文件导读
// 可信资讯来源登记工具：解析来源配置、验证人物和HTTPS来源、幂等写入与分页。来源feed身份与具体文章地址分开。
// 阅读顺序：输入数据结构 → 返回状态/可选数据 → 方法声明 → 私有成员。引用/指针保存依赖，必须确认由谁拥有及何时释放。
// <<< MAIN-DEFENSE
#pragma once

#include "care_contract.h"
#include <QSqlDatabase>
#include <QUrl>

namespace person_source_config {
// >>> MAIN-DEFENSE | 接口结构｜Entry
// 这是数据包：检查字段含义、默认值和optional缺失状态。
// <<< MAIN-DEFENSE
struct Entry {
    QString personId;
    QUrl feedUrl;
};
// >>> MAIN-DEFENSE | 接口结构｜Batch
// 这是数据包：检查字段含义、默认值和optional缺失状态。
// <<< MAIN-DEFENSE
struct Batch {
    QString provenance;
    QList<Entry> entries;
};
// >>> MAIN-DEFENSE | 接口结构｜Report
// 这是数据包：检查字段含义、默认值和optional缺失状态。
// <<< MAIN-DEFENSE
struct Report {
    int inserted = 0;
    int unchanged = 0;
};
// >>> MAIN-DEFENSE | 接口结构｜Source
// 这是数据包：检查字段含义、默认值和optional缺失状态。
// <<< MAIN-DEFENSE
struct Source {
    QString personId;
    QUrl feedUrl;
    std::optional<qint64> lastSuccessUtc;
    bool stale = false;
    qint64 refreshGeneration = 0;
};

// Parses a server-local, operator-authored JSON configuration. It performs no file
// or network I/O and never derives feed URLs from person identities or articles.
// >>> MAIN-DEFENSE | 方法声明｜parse
// 本行约定调用参数和返回类型；具体分块处理在对应cpp，调用者须遵守已有注释中的权限与生命周期约定。
// <<< MAIN-DEFENSE
care_contract::Result<Batch> parse(const QByteArray& json);

// Additive and idempotent. The batch is transactional; missing people, malformed
// URLs or database failures roll back the entire batch. Existing rows and their
// cached refresh state are never modified or deleted.
// >>> MAIN-DEFENSE | 方法声明｜registerBatch
// 验证可信来源配置并在数据库登记，保持可重复运行，不接受客户端任意抓取地址。
// <<< MAIN-DEFENSE
care_contract::Result<Report> registerBatch(QSqlDatabase& db, const Batch& batch);
// >>> MAIN-DEFENSE | 方法声明｜registerFile
// 读取有大小限制的来源配置文件，再委托parse和registerBatch。
// <<< MAIN-DEFENSE
care_contract::Result<Report> registerFile(QSqlDatabase& db, const QString& path);

// Server-internal read interfaces for the provider. Neither function exposes
// person sources directly to a network client.
care_contract::Result<care_contract::Page<Source>> listSources(
    QSqlDatabase& db, const care_contract::PageRequest& page);
care_contract::Result<care_contract::Page<Source>> listSourcesForPerson(
    QSqlDatabase& db, const QString& personId, const care_contract::PageRequest& page);
} // namespace person_source_config
