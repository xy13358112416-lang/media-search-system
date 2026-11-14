# 仓库智能推荐接口草案

此契约供主逻辑、Qt 和推荐同学确认；当前仅定义类型和协议，不代表服务器已经接入 LLM。客户端不得直接调用 DeepSeek 或提交用户 ID、候选资源；服务器从当前连接的 token 确认身份和候选访问权限。

`WarehouseService::recommendResources(request, callback)` 接收非空 `query`（最多 1000 个 UTF-16 代码单元）、`limit`（1–50）和可选的 `targetWarehouseId`：编辑现有仓库时使用，创建之前可留空。它返回不透明的 `RecommendationHandle`，并通过回调返回 `Result<WarehouseRecommendationResult>`。参数或连接等立即失败时可以同步调用回调并返回空句柄；非空句柄只在创建它的客户端对象和当前连接会话内有效。结果包含 `items`、整体说明 `message` 和 `fallback` 标志；每个条目包含来源仓库 ID、条目 ID、完整媒体记录和单条推荐理由 `reason`。`message` 可说明无匹配或降级；成功且没有匹配是正常的空列表，不视为网络失败，也不自动填充热门作品。

网络请求示例：

```json
{"type":"recommend_resources","req_id":"12","token":"当前连接令牌","payload":{"query":"科幻","limit":5,"warehouse_id":"3"}}
```

创建仓库前不发送 `warehouse_id`。成功响应中，`items` 是数组，每项包含十进制字符串 `warehouse_id`、`item_id`，媒体对象 `media` 和字符串 `reason`；`fallback` 是布尔值，`message` 是字符串，允许为空。具体示例及帧协议见 `network/README.md` 的后续版本。

Qt 的取消按钮调用 `cancelRecommendation(handle)`。若原请求仍待处理，它返回 true，并恰好一次以 `Cancelled` 完成原回调；句柄无效或原回调已完成时返回 false，不再次调用原回调。取消与正常结果同时到达时，客户端所属线程中先完成原请求的事件生效，另一个结果丢弃。超时、退出、断线也各自只完成一次；迟到响应忽略。

客户端尽力发送独立的 `cancel_recommendation` 请求，其 `payload.target_req_id` 指向同一连接中原推荐请求的网络编号。取消响应使用取消请求自己的 `req_id` 和 `cancel_recommendation_resp`（空 `payload`），仅确认服务端接收了取消操作，不表示模型一定已停止；原推荐回调不等待此确认。网络断开时不能保证取消消息送达。对于已发送的写请求，`OutcomeUnknown` 表示可能已经保存，应查询确认后再决定是否重试。

后续服务端接线必须验证目标仓库权限，只把当前用户拥有或公开可见的条目作为候选，确保模型输出只能引用已授权候选；返回前再次检查会话及访问权限。退出、断开、取消时终止或丢弃对应任务，避免结果跨连接、跨账号。
