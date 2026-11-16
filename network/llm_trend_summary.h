#pragma once

#include "care_contract.h"
#include "llm_recommendation.h"

namespace llm_trend_summary {
using Config = llm_recommendation::Config;

// Optional SERVER-ONLY text layer. The caller must supply the already-authorized,
// privacy-thresholded public aggregate, and recheck session/visibility on delivery.
// No authentication, aggregation, history lookup or configuration loading occurs here.
class Client final : public QObject {
public:
    using Completion = std::function<void(care_contract::TrendingResult)>;
    explicit Client(Config config, QObject* parent = nullptr);
    ~Client() override;
    // Always asynchronous on this object's thread; no paid retries. Original rows,
    // counts, range and provenance are unchanged. Failure preserves the rule summary.
    // Model text is interpretation, not verified news; render summary as plain text.
    quint64 summarize(care_contract::TrendingResult authorized, Completion completion);
    // Suppresses completion, including queued no-key/empty paths. Thread-affine.
    void cancel(quint64 requestId);
private:
    struct Job;
    void complete(const std::shared_ptr<Job>& job, care_contract::TrendingResult result);
    Config config_;
    QNetworkAccessManager manager_;
    QHash<quint64, std::shared_ptr<Job>> jobs_;
    quint64 nextId_ = 0;
};
} // namespace llm_trend_summary
