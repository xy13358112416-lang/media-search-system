// >>> DEFENSE-NOTE | 头文件导读｜实时联网场景选择接口
// 输入明确场景和已授权候选；Result.ids是模型选中的作品ID，fallback标志区分失败回退与成功空结果。
// 读法：struct说明传哪些数据；class/函数声明说明谁可调用；实际算法去同名.cpp或引用的实现文件。
// <<< DEFENSE-NOTE | 本段讲解结束
#pragma once
#include "llm_recommendation.h"
#include "care_contract.h"

namespace llm_scene {
struct Result {
    QStringList ids;
    bool fallback = true;
    QString message;
};
// Server-only live web search. No persistent cache, user identity or history is sent.
class Client final : public QObject {
public:
    using Completion = std::function<void(Result)>;
    explicit Client(llm_recommendation::Config config, QObject* parent = nullptr);
    ~Client() override;
    quint64 recommend(care_contract::BrowseScene scene, QList<llm_recommendation::Candidate> candidates,
                      int limit, Completion done);
    void cancel(quint64 id);
private:
    struct Job;
    void finish(const std::shared_ptr<Job>& job, Result result);
    llm_recommendation::Config config_;
    QNetworkAccessManager manager_;
    QHash<quint64, std::shared_ptr<Job>> jobs_;
    quint64 nextId_ = 0;
};
}
