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
