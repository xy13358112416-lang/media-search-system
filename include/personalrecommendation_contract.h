#pragma once

#include <QString>
#include <QStringList>
#include <QtGlobal>

// 个性化推荐的共享约定；本文件仅声明类型和服务入口，不实现推荐或数据库访问。
namespace recommendation_contract {

enum class PreferenceMode {
    Auto,     // 根据当前用户的历史推断；没有历史时不使用该字段的个人偏好。
    Any,      // 不使用该字段的偏好，也不从历史补全。
    Explicit  // 使用 value 中明确指定的类别或文件类型。
};

struct PreferenceSelection {
    PreferenceMode mode = PreferenceMode::Auto;
    // Auto / Any 时忽略 value；Explicit 时填写实际选项值，不用空字符串区分模式。
    // 明确选择表示推荐偏好，沿用现有推荐的加分语义，不是检索的硬性过滤条件。
    QString value;
};

struct RecommendationRequest {
    // 与账户草案使用相同的 qint64 类型。0 表示尚无用户，不能代替真实登录身份。
    // 后续服务实现须核对主逻辑的当前会话，不能只信任调用方提供的 ID。
    qint64 userId = 0;
    PreferenceSelection category;
    PreferenceSelection fileType;
    int limit = 5; // 与现有推荐一致：小于等于零时，已验证身份的请求成功返回空列表。
};

struct RecommendationResult {
    // 成功但没有结果时为 true 且 mediaIds 为空；失败时为 false，并填写 error。
    // 读取历史失败是错误，不能当成“没有历史”静默改用热门推荐。
    bool success = false;
    QString error;       // 失败时可供界面显示；成功时为空。
    QStringList mediaIds; // 成功时按推荐顺序排列；Qt 不再自行排序。失败时为空。
    QString reason;      // 成功时的整体推荐依据，供推荐页说明区域显示。失败时为空。
};

class PersonalRecommendationService {
public:
    virtual ~PersonalRecommendationService() = default;

    // 后续实现通过主逻辑接口获取会话、当前媒体和个人历史，再调用推荐算法。
    // Qt 只依赖此入口，无须访问 SQLite。会话不匹配或历史读取失败时返回失败。
    virtual RecommendationResult recommend(const RecommendationRequest& request) = 0;
};

} // namespace recommendation_contract
