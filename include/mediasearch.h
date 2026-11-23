// >>> DEFENSE-NOTE | 头文件导读｜检索接口
// SearchQuery是输入；MediaSearch保存ID→记录的QHash及字段→有序ID列表的倒排表。search返回ID而不是界面控件。
// 读法：struct说明传哪些数据；class/函数声明说明谁可调用；实际算法去同名.cpp或引用的实现文件。
// <<< DEFENSE-NOTE | 本段讲解结束
#pragma once

#include "mediarecord.h"
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

// 各项去除首尾空白后，非空条件按 AND 组合。导演和演员按完整姓名匹配。
// >>> DEFENSE-NOTE | 类型说明｜SearchQuery
// 检索条件：非空导演/演员/格式按AND组合，姓名精确匹配；留空表示此字段不限。
// <<< DEFENSE-NOTE | 本段讲解结束
struct SearchQuery {
    QString director;  // 导演姓名，留空时不限导演。
    QString actor;     // 一名演员的完整姓名，留空时不限演员。
    QString fileType;  // 文件扩展名，例如 MP4。忽略大小写，留空时不限类型。
};

// 三种策略采用相同的匹配规则，供联调与对照实验选择。
enum class SearchStrategy { LinearScan, FixedIndexOrder, SmallestListFirst };
enum class SearchField { Director, Actor, FileType };

// >>> DEFENSE-NOTE | 类型说明｜SearchStep
// 一次索引步骤的统计：postingCount为单条件列表长度，remainingCount为交集后候选数。
// <<< DEFENSE-NOTE | 本段讲解结束
struct SearchStep {
    SearchField field;            // 本步使用的查询字段。
    qsizetype postingCount = 0;   // 该条件单独命中的 ID 数。
    qsizetype remainingCount = 0; // 应用该条件后剩余的候选数。
};

// >>> DEFENSE-NOTE | 类型说明｜SearchTrace
// 性能探针：recordsScanned记录线性扫描规模，steps记录索引执行顺序；不是推荐分数。
// <<< DEFENSE-NOTE | 本段讲解结束
struct SearchTrace {
    qsizetype recordsScanned = 0; // 逐条扫描访问的记录数。
    QList<SearchStep> steps;      // 实际执行的索引步骤，按执行顺序排列。
};

class MediaSearch {
public:
    // 用 records 中的全部记录替换现有索引。主程序在加载成功后调用。
    // ID 为空或重复时抛出 std::invalid_argument，现有索引保持不变。
    // >>> DEFENSE-NOTE | 接口翻译｜rebuild
    // 完整重建记录与倒排表；调用者在加载全部数据成功后使用。
    // <<< DEFENSE-NOTE | 本段讲解结束
    void rebuild(const QList<MediaRecord>& records);

    // 将已保存的 record 加入索引。ID 为空或重复时抛出 std::invalid_argument。
    // >>> DEFENSE-NOTE | 接口翻译｜addRecord
    // 已持久化的新记录加入内存索引；不是创建主键或写数据库。
    // <<< DEFENSE-NOTE | 本段讲解结束
    void addRecord(const MediaRecord& record);

    // 从索引移除已删除的记录。id 在索引中不存在时抛出 std::out_of_range。
    // >>> DEFENSE-NOTE | 接口翻译｜removeRecord
    // 根据ID及原记录字段删除索引关联；调用者负责持久化流程。
    // <<< DEFENSE-NOTE | 本段讲解结束
    void removeRecord(const QString& id);

    // 默认用短列表优先策略。返回匹配的媒体 ID，按 ID 升序排列，无匹配时返回空列表。
    // 全部条件留空时返回所有 ID。列表长度即命中数量。
    QStringList search(const SearchQuery& query) const;

    // 使用指定 strategy；传入 trace 时会重置并填充本次执行过程。
    QStringList search(const SearchQuery& query, SearchStrategy strategy,
                       SearchTrace* trace = nullptr) const;

private:
    QHash<QString, MediaRecord> records_;  // 按唯一 ID 保存已规范化的媒体记录。
    QHash<QString, QStringList> directors_;  // 导演到有序 ID 列表的索引。
    QHash<QString, QStringList> actors_;     // 演员到有序 ID 列表的索引。
    QHash<QString, QStringList> fileTypes_;  // 扩展名到有序 ID 列表的索引。

    // 批量重建时 appendOnly 为 true，先追加，最后统一排序。
    void indexRecord(const MediaRecord& record, bool appendOnly);
};
