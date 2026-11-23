// >>> MAIN-DEFENSE | 主逻辑头文件导读
// 共享接口/数据契约：上层通过这里的类型和方法与主逻辑对接。声明不等于执行，实际实现见对应cpp。
// 阅读顺序：输入数据结构 → 返回状态/可选数据 → 方法声明 → 私有成员。引用/指针保存依赖，必须确认由谁拥有及何时释放。
// <<< MAIN-DEFENSE
#pragma once
#include "mediarecord.h"
#include <QList>
#include <QString>
#include <QtGlobal>
#include <functional>
#include <optional>
#include <utility>

namespace warehouse_contract {
enum class ErrorCode { None, InvalidArgument, NotConnected, NotAuthenticated, AuthenticationFailed, PermissionDenied,
                       NotFound, Timeout, ProtocolError, ServerError, Cancelled, OutcomeUnknown };
// >>> MAIN-DEFENSE | 接口结构｜Status
// 这是数据包：检查字段含义、默认值和optional缺失状态。
// <<< MAIN-DEFENSE
struct Status { ErrorCode code = ErrorCode::None; QString message; bool ok() const { return code == ErrorCode::None; } };
template<class T> struct Result { Status status; std::optional<T> value; };
enum class Visibility { Private, Public };
enum class LoginRole { User, Administrator };
struct UserInfo { qint64 userId = 0; QString username; bool isAdmin = false; };
// >>> MAIN-DEFENSE | 接口结构｜UserInfo
// 这是数据包：检查字段含义、默认值和optional缺失状态。
// <<< MAIN-DEFENSE
// >>> MAIN-DEFENSE | 接口结构｜WarehouseSummary
// 这是数据包：检查字段含义、默认值和optional缺失状态。
// <<< MAIN-DEFENSE
struct WarehouseSummary { qint64 warehouseId = 0; QString name; qint64 ownerId = 0; Visibility visibility = Visibility::Private; };
// >>> MAIN-DEFENSE | 接口结构｜WarehouseItem
// 这是数据包：检查字段含义、默认值和optional缺失状态。
// <<< MAIN-DEFENSE
struct WarehouseItem { qint64 itemId = 0; MediaRecord media; };
// >>> MAIN-DEFENSE | 接口结构｜WarehouseDetail
// 这是数据包：检查字段含义、默认值和optional缺失状态。
// <<< MAIN-DEFENSE
struct WarehouseDetail { WarehouseSummary warehouse; QList<WarehouseItem> items; };
// 创建仓库时 targetWarehouseId 为空；编辑仓库时提供其编号。服务器确定可访问的候选。
// >>> MAIN-DEFENSE | 接口结构｜WarehouseRecommendationRequest
// 这是数据包：检查字段含义、默认值和optional缺失状态。
// <<< MAIN-DEFENSE
struct WarehouseRecommendationRequest { QString query; std::optional<qint64> targetWarehouseId; int limit = 5; };
// >>> MAIN-DEFENSE | 接口结构｜WarehouseRecommendationItem
// 这是数据包：检查字段含义、默认值和optional缺失状态。
// <<< MAIN-DEFENSE
struct WarehouseRecommendationItem { qint64 warehouseId = 0; qint64 itemId = 0; MediaRecord media; QString reason; };
// >>> MAIN-DEFENSE | 接口结构｜WarehouseRecommendationResult
// 这是数据包：检查字段含义、默认值和optional缺失状态。
// <<< MAIN-DEFENSE
struct WarehouseRecommendationResult { QList<WarehouseRecommendationItem> items; bool fallback = false; QString message; };
// 非空句柄只在创建它的客户端对象及当前连接会话内有效。
using RecommendationHandle = QString;
template<class T> using Callback = std::function<void(Result<T>)>;
using StatusCallback = std::function<void(Status)>;

// 回调在所属线程触发且每个请求恰好一次；调用者不得传入用户身份。
// >>> MAIN-DEFENSE | 接口结构｜WarehouseService
// 这是服务对象：public为调用入口，private保存内部状态；Qt或其他模块通过接口调用，业务权限在实现中校验。
// <<< MAIN-DEFENSE
class WarehouseService {
public:
    // >>> MAIN-DEFENSE | 方法声明｜WarehouseService
    // 本行约定调用参数和返回类型；具体分块处理在对应cpp，调用者须遵守已有注释中的权限与生命周期约定。
    // <<< MAIN-DEFENSE
    virtual ~WarehouseService() = default;
    // >>> MAIN-DEFENSE | 方法声明｜connectToServer
    // 异步建立TCP连接并设超时；连接代次避免旧回调覆盖新连接。
    // <<< MAIN-DEFENSE
    virtual void connectToServer(const QString& host, quint16 port, StatusCallback done) = 0;
    // >>> MAIN-DEFENSE | 方法声明｜disconnectFromServer
    // 主动断开连接，清token并处理所有等待请求，避免新账号收到旧结果。
    // <<< MAIN-DEFENSE
    virtual void disconnectFromServer() = 0;
    // >>> MAIN-DEFENSE | 方法声明｜login
    // 按规范化用户名读密码哈希，验证明文输入；成功后建立当前会话，失败不能保留旧身份。
    // <<< MAIN-DEFENSE
    virtual void login(const QString& username, const QString& password, Callback<UserInfo> done) = 0;
    virtual void loginAs(const QString& username, const QString& password, LoginRole role, Callback<UserInfo> done)
    {
        login(username, password, [this, role, done=std::move(done)](Result<UserInfo> result) {
            if (result.status.ok() && result.value &&
                result.value->isAdmin != (role == LoginRole::Administrator)) {
                disconnectFromServer();
                done({{ErrorCode::PermissionDenied, QStringLiteral("账号角色与登录入口不匹配。")}, {}});
                return;
            }
            done(std::move(result));
        });
    }
    // >>> MAIN-DEFENSE | 方法声明｜logout
    // 清当前登录状态，使后续受保护操作不能沿用退出前身份。
    // <<< MAIN-DEFENSE
    virtual void logout(StatusCallback done) = 0;
    // >>> MAIN-DEFENSE | 方法声明｜listMyWarehouses
    // 只查询当前会话拥有的仓库，身份从token解析。
    // <<< MAIN-DEFENSE
    virtual void listMyWarehouses(Callback<QList<WarehouseSummary>> done) = 0;
    // >>> MAIN-DEFENSE | 方法声明｜listPublicWarehouses
    // 查询公开仓库；公开可读不代表所有人可增删。
    // <<< MAIN-DEFENSE
    virtual void listPublicWarehouses(Callback<QList<WarehouseSummary>> done) = 0;
    // >>> MAIN-DEFENSE | 方法声明｜getWarehouse
    // 获取一个有读权限仓库的详情与条目。
    // <<< MAIN-DEFENSE
    virtual void getWarehouse(qint64 warehouseId, Callback<WarehouseDetail> done) = 0;
    // >>> MAIN-DEFENSE | 方法声明｜createWarehouse
    // 创建归当前用户所有的公开或私有仓库。
    // <<< MAIN-DEFENSE
    virtual void createWarehouse(const QString& name, Visibility visibility, Callback<WarehouseSummary> done) = 0;
    // >>> MAIN-DEFENSE | 方法声明｜addItem
    // 向目标仓库新增条目；最终写权限必须由服务端检查owner。
    // <<< MAIN-DEFENSE
    virtual void addItem(qint64 warehouseId, const MediaRecord& media, Callback<WarehouseItem> done) = 0;
    // >>> MAIN-DEFENSE | 方法声明｜removeItem
    // 从目标仓库删除指定条目；不允许以客户端按钮状态代替owner校验。
    // <<< MAIN-DEFENSE
    virtual void removeItem(qint64 warehouseId, qint64 itemId, StatusCallback done) = 0;
    // 立即失败时可同步回调并返回空句柄；否则返回供取消使用的不透明句柄。
    // >>> MAIN-DEFENSE | 方法声明｜recommendResources
    // 本行约定调用参数和返回类型；具体分块处理在对应cpp，调用者须遵守已有注释中的权限与生命周期约定。
    // <<< MAIN-DEFENSE
    virtual RecommendationHandle recommendResources(const WarehouseRecommendationRequest& request, Callback<WarehouseRecommendationResult> done) = 0;
    // true 表示本地成功取消等待并以 Cancelled 完成原回调；false 表示已完成或无效。
    // >>> MAIN-DEFENSE | 方法声明｜cancelRecommendation
    // 通过请求句柄取消模型类任务并抑制旧结果交付。
    // <<< MAIN-DEFENSE
    virtual bool cancelRecommendation(const RecommendationHandle& handle) = 0;
};
}
