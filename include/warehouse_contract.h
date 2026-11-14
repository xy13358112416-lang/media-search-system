#pragma once
#include "mediarecord.h"
#include <QList>
#include <QString>
#include <QtGlobal>
#include <functional>
#include <optional>

namespace warehouse_contract {
enum class ErrorCode { None, InvalidArgument, NotConnected, NotAuthenticated, AuthenticationFailed, PermissionDenied,
                       NotFound, Timeout, ProtocolError, ServerError, Cancelled, OutcomeUnknown };
struct Status { ErrorCode code = ErrorCode::None; QString message; bool ok() const { return code == ErrorCode::None; } };
template<class T> struct Result { Status status; std::optional<T> value; };
enum class Visibility { Private, Public };
struct UserInfo { qint64 userId = 0; QString username; };
struct WarehouseSummary { qint64 warehouseId = 0; QString name; qint64 ownerId = 0; Visibility visibility = Visibility::Private; };
struct WarehouseItem { qint64 itemId = 0; MediaRecord media; };
struct WarehouseDetail { WarehouseSummary warehouse; QList<WarehouseItem> items; };
// 创建仓库时 targetWarehouseId 为空；编辑仓库时提供其编号。服务器确定可访问的候选。
struct WarehouseRecommendationRequest { QString query; std::optional<qint64> targetWarehouseId; int limit = 5; };
struct WarehouseRecommendationItem { qint64 warehouseId = 0; qint64 itemId = 0; MediaRecord media; QString reason; };
struct WarehouseRecommendationResult { QList<WarehouseRecommendationItem> items; bool fallback = false; QString message; };
// 非空句柄只在创建它的客户端对象及当前连接会话内有效。
using RecommendationHandle = QString;
template<class T> using Callback = std::function<void(Result<T>)>;
using StatusCallback = std::function<void(Status)>;

// 回调在所属线程触发且每个请求恰好一次；调用者不得传入用户身份。
class WarehouseService {
public:
    virtual ~WarehouseService() = default;
    virtual void connectToServer(const QString& host, quint16 port, StatusCallback done) = 0;
    virtual void disconnectFromServer() = 0;
    virtual void login(const QString& username, const QString& password, Callback<UserInfo> done) = 0;
    virtual void logout(StatusCallback done) = 0;
    virtual void listMyWarehouses(Callback<QList<WarehouseSummary>> done) = 0;
    virtual void listPublicWarehouses(Callback<QList<WarehouseSummary>> done) = 0;
    virtual void getWarehouse(qint64 warehouseId, Callback<WarehouseDetail> done) = 0;
    virtual void createWarehouse(const QString& name, Visibility visibility, Callback<WarehouseSummary> done) = 0;
    virtual void addItem(qint64 warehouseId, const MediaRecord& media, Callback<WarehouseItem> done) = 0;
    virtual void removeItem(qint64 warehouseId, qint64 itemId, StatusCallback done) = 0;
    // 立即失败时可同步回调并返回空句柄；否则返回供取消使用的不透明句柄。
    virtual RecommendationHandle recommendResources(const WarehouseRecommendationRequest& request, Callback<WarehouseRecommendationResult> done) = 0;
    // true 表示本地成功取消等待并以 Cancelled 完成原回调；false 表示已完成或无效。
    virtual bool cancelRecommendation(const RecommendationHandle& handle) = 0;
};
}
