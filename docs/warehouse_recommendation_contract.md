# 仓库智能推荐接口草案

此契约供主逻辑、Qt 和推荐同学确认；当前仅定义类型和协议，不代表服务器已经接入 LLM。客户端不得直接调用 DeepSeek 或提交用户 ID、候选资源；服务器从当前连接的 token 确认身份和候选访问权限。

`WarehouseService::recommendResources(request, callback)` 异步返回 `Result<WarehouseRecommendationResult>`。请求包含非空 `query`（最多 1000 个 UTF-16 代码单元）、`limit`（1–50）和可选的 `targetWarehouseId`：编辑现有仓库时使用，创建之前可留空。结果包含 `items` 和 `fallback` 标志；每个条目包含来源仓库 ID、条目 ID、完整媒体记录和推荐理由。请求发送、参数错误、认证失败等由 `Status` 返回。

网络请求示例：

```json
{"type":"recommend_resources","req_id":"12","token":"当前连接令牌","payload":{"query":"科幻","limit":5,"warehouse_id":"3"}}
```

创建仓库前不发送 `warehouse_id`。成功响应中，`items` 是数组，每项包含十进制字符串 `warehouse_id`、`item_id`，媒体对象 `media` 和字符串 `reason`；`fallback` 是布尔值。具体示例及帧协议见 `network/README.md` 的后续版本。

推荐超时或退出时可发送新请求 `cancel_recommendation`，其 `payload.target_req_id` 是同一连接中原推荐请求的编号。客户端只处理仍在等待的响应，迟到结果丢弃；网络断开时不能保证取消消息送达。断线、超时、取消等通过状态返回。对于已发送的写请求，`OutcomeUnknown` 表示可能已经保存，应查询确认后再决定是否重试。

后续服务端接线必须验证目标仓库权限，只把当前用户拥有或公开可见的条目作为候选，确保模型输出只能引用已授权候选；返回前再次检查会话及访问权限。退出、断开、取消时终止或丢弃对应任务，避免结果跨连接、跨账号。
