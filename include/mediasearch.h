#pragma once

#include "mediarecord.h"
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

// 各项去除首尾空白后，非空条件按 AND 组合。创作者和演员按完整姓名匹配。
struct SearchQuery {
    QString creator;  // 创作者姓名，留空时不限创作者。
    QString actor;     // 一名演员的完整姓名，留空时不限演员。
    QString fileType;  // 文件扩展名，例如 MP4。忽略大小写，留空时不限类型。
};

// 三种策略采用相同的匹配规则，供联调与对照实验选择。
enum class SearchStrategy { LinearScan, FixedIndexOrder, SmallestListFirst };
enum class SearchField { Creator, Actor, FileType };

struct SearchStep {
    SearchField field;            // 本步使用的查询字段。
    qsizetype postingCount = 0;   // 该条件单独命中的 ID 数。
    qsizetype remainingCount = 0; // 应用该条件后剩余的候选数。
};

struct SearchTrace {
    qsizetype recordsScanned = 0; // 逐条扫描访问的记录数。
    QList<SearchStep> steps;      // 实际执行的索引步骤，按执行顺序排列。
};

class MediaSearch {
public:
    // 用 records 中的全部记录替换现有索引。主程序在加载成功后调用。
    // ID 为空或重复时抛出 std::invalid_argument，现有索引保持不变。
    void rebuild(const QList<MediaRecord>& records);

    // 将已保存的 record 加入索引。ID 为空或重复时抛出 std::invalid_argument。
    void addRecord(const MediaRecord& record);

    // 从索引移除已删除的记录。id 在索引中不存在时抛出 std::out_of_range。
    void removeRecord(const QString& id);

    // 默认用短列表优先策略。返回匹配的媒体 ID，按 ID 升序排列，无匹配时返回空列表。
    // 全部条件留空时返回所有 ID。列表长度即命中数量。
    QStringList search(const SearchQuery& query) const;

    // 使用指定 strategy；传入 trace 时会重置并填充本次执行过程。
    QStringList search(const SearchQuery& query, SearchStrategy strategy,
                       SearchTrace* trace = nullptr) const;

private:
    QHash<QString, MediaRecord> records_;  // 按唯一 ID 保存已规范化的媒体记录。
    QHash<QString, QStringList> creators_;  // 创作者到有序 ID 列表的索引。
    QHash<QString, QStringList> actors_;     // 演员到有序 ID 列表的索引。
    QHash<QString, QStringList> fileTypes_;  // 扩展名到有序 ID 列表的索引。

    // 批量重建时 appendOnly 为 true，先追加，最后统一排序。
    void indexRecord(const MediaRecord& record, bool appendOnly);
};
