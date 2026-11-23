// >>> DEFENSE-NOTE | 头文件导读｜候选内模型推荐接口
// Config只在服务器配置；Request带已授权候选；Client异步返回任务ID，Completion完成时交付Result，cancel不回调。
// 读法：struct说明传哪些数据；class/函数声明说明谁可调用；实际算法去同名.cpp或引用的实现文件。
// <<< DEFENSE-NOTE | 本段讲解结束
#pragma once

#include "mediarecord.h"
#include <QObject>
#include <QNetworkAccessManager>
#include <QHash>
#include <QUrl>
#include <functional>
#include <memory>

namespace llm_recommendation {
// >>> DEFENSE-NOTE | 类型说明｜Config
// 服务器模型配置：apiKey、端点、模型名与超时；此对象不能发送到Qt客户端或写入公共日志。
// <<< DEFENSE-NOTE | 本段讲解结束
struct Config {
    QString apiKey;
    QUrl endpoint{QStringLiteral("https://api.deepseek.com/chat/completions")};
    QString model = QStringLiteral("deepseek-flash");
    int timeoutMs = 15000;
};
// File: explicit path > MEDIA_SERVER_DEEPSEEK_CONFIG_PATH > AppConfigLocation/llm_config.json.
// MEDIA_SERVER_DEEPSEEK_API_KEY overrides the file key. Configure on the server only.
// The general DEEPSEEK_API_KEY is deliberately ignored to isolate other projects.
Config loadConfig(const QString& configPath = {});
// >>> DEFENSE-NOTE | 类型说明｜Candidate
// 模型候选：不歧义key及有限媒体信息；key由业务层提供，不是模型自行发明。
// <<< DEFENSE-NOTE | 本段讲解结束
struct Candidate {
    QString key; // Globally unambiguous opaque key supplied by the warehouse layer, not a local media ID.
    MediaRecord media;
};
struct Request {
    QString context; // Warehouse theme/description or user-provided search intent.
    // Caller MUST supply only resources authorized by the server for the current session.
    // This client neither authenticates warehouse access nor enumerates private resources.
    QList<Candidate> candidates;
    int limit = 6;
};
// >>> DEFENSE-NOTE | 类型说明｜Item
// 模型推荐条目：原候选key和纯文本理由；返回时仍需候选与权限复核。
// <<< DEFENSE-NOTE | 本段讲解结束
struct Item { QString key; QString reason; };
struct Result {
    bool fallback = false;
    // fallback=false + empty items can mean the model found no related candidates.
    QList<Item> items; // Keys from this request only, in recommended order; render reasons as plain text.
    QString message;
};
class Client final : public QObject {
public:
    using Completion = std::function<void(Result)>;
    explicit Client(Config config, QObject* parent = nullptr);
    ~Client() override;
    // All completions are asynchronous on this object's thread. No automatic paid retries.
    quint64 recommend(Request request, Completion completion);
    // Cancellation suppresses completion; call when account/warehouse changes.
    void cancel(quint64 requestId);
private:
    struct Job;
    void complete(const std::shared_ptr<Job>& job, Result result);
    Config config_;
    QNetworkAccessManager manager_;
    QHash<quint64, std::shared_ptr<Job>> jobs_;
    quint64 nextId_ = 0;
};
} // namespace llm_recommendation
