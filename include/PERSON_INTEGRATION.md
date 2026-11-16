# 人物与模型摘要：本次交付和后续接入边界

本文件是源码接口说明。交接计划中的 N00 / N06 / N09 是任务号，不是 GitHub PR 号。

## 已交付的我方代码

- `care_person_snapshot`：纯人物适配器，接收服务端可信目录，按 `(workId, warehouseId, itemId)` 精确匹配当前授权作品；不按姓名或本地 CSV ID 合并人物。同名不同 ID 独立，多角色排序去重，每作品最多 256 个已确认人物。目录读取失败返回错误。
- `WarehouseServer::setPersonContextLoader`：真实 `get_resource` / `record_behavior` / `recommend_temporal` 已调用的服务端接入口。身份来自认证会话，授权完成后才传入当前资源；不是新客户端参数或新 RPC。
- 新浏览的已确认人物写入已有 `behavior_events.media_snapshot` JSON 的 `_care_people_v1` 字段，读取后进入足迹和时序推荐。没有改数据库 schema。没有该字段的旧事件仍表示当时人物未知；不会用今天目录补造旧人物。重试先查原事件，暂停历史不调用目录。目录异常不新增半份行为。
- `llm_trend_summary::Client`：独立、异步的服务端热点模型标题/摘要模块，具有取消、超时、响应/请求预算和规则摘要回退。自动测试只访问本机假 HTTP，不产生 API 费用。
- `sample-data/person/`：**明确标记为虚构的测试夹具**，验证同名、多角色、未知信息和来源/日期格式。不是已爬取的真实人物资料或生产导入器。

## 主逻辑的 N03 接法

`server/person_store.h` 是内部仓储边界，现由 `person_store::SqliteStore` 实现；服务器在打开数据库后默认装配人物 context。沿用现有服务端数据库和线程，不创建另一份媒体库，不把 SQLite 交给客户端。

需要替换默认实现或在测试中注入目录时，可在服务器启动时装配：

```cpp
server.setPersonContextLoader([&store](qint64 authenticatedUserId,
    const QList<care_contract::ResourceRef>& authorizedResources) {
    return store.contextFor(authenticatedUserId, authorizedResources);
});
```

`store` 必须比服务器及其加载器活得更久。加载器同步运行在服务器/数据库所属线程，不发网络请求，不启动嵌套事件循环。数据库失败返回错误；未知人物成功返回空映射。目录返回的 `PersonContext` 是当前用户作用域，不能直接由客户端 JSON 构造。

可信目录/作品关联可由服务端操作工具 `media_person_import` 导入，格式与事务约定见 `server/PERSON_IMPORT.md`。必须核对服务器登记的完整作品来源三元组；导入不推断 CSV 同名身份。虚构样例只用于显式允许的独立演示库。人物 RPC/provider 和页面装配仍是后续 Qt 对接任务，不能把导入测试通过等同于这些链路已完成。

加载器未设置时保持原有 `people=[]` 和无人物关注加分，不声称人物中心已经联网。既有历史人物快照即使未装配目录或作品已删除，也仍可用于本人足迹。`followedPeople` 只影响当前候选，默认 `+0.05`；关闭个性化忽略关注加分，不感兴趣仍硬排除。主动关注不是隐式历史，清历史不得删除关注表。

**历史存储只保留当时姓名/人物 ID/角色，不保留 followed 标记。** 如需未来迁移或导入历史人物，必须有当时可信证据，不按当前姓名批量回填。

## Qt 的 N02 / N04 / N05 接法

- 补人物 5 个客户端方法和 4 个服务端只读操作；复用 `care_person` 搜索、作品与资讯缓存算法。`set_follow` 的写事务仍由主逻辑仓储负责，客户端加入 mutation 判定保留 OutcomeUnknown。
- Qt 写服务端人物读服务/分页/JSON 响应和资讯 provider；主逻辑提供授权仓储。资讯 provider 异步刷新，标准化后调用 `mergeUpdates`，不直接创建表，不允许客户端指定任意抓取 URL。
- 资讯夹具中的 `example.invalid` 是保留示例域名，不能拿它当真实来源或演示新闻。真实来源未选定/核验前，News 不算交付。

## Qt 的 N09 服务适配接法

构建目标为 `llm_trend_summary`；头文件 `network/llm_trend_summary.h`。与已有仓库推荐共享 `llm_recommendation::Config`，只有服务器调用 `loadConfig()`；配置变量仍为 `MEDIA_SERVER_DEEPSEEK_API_KEY`。

```cpp
// 在服务器所属线程创建；对象应持续存在，不在栈上立即销毁。
llm_trend_summary::Client summaryClient(llm_recommendation::loadConfig());
const auto job = summaryClient.summarize(alreadyAuthorizedTrendingResult,
    [](care_contract::TrendingResult result) {
        // 网络服务适配层：复查会话与当前公共资格后返回。
        // GUI按纯文本显示，不能当HTML或Markdown执行。
    });
// 断线、账号失效或请求取消时：summaryClient.cancel(job);
```

仅发送当前公开榜单的截短标题/搜索词和聚合计数，不发送用户 ID、原始行为、资源 ID、人物、导演或其他媒体元数据。模型返回支持行号、标题和无数字的解释文本；程序验证行号、长度、格式，并由本地追加精确计数。模型解释不等于经过核实的新闻，也不能保证没有定性误判。

无 Key、空榜、坏数据、超时、HTTP/格式错误时保留规则摘要，`summaryFromModel=false`。成功才置 true；原始榜单条目、排序、区间、计数、生成时间和模拟标记不改。不自动重试、不跟随重定向。

**本次未把 `get_trending` 改成异步网络操作，也未装配页面。** Qt 后续须把此模块接到真实服务器请求生命周期：认证后聚合，显式启动异步摘要，超时/取消清理，发送前重新检查会话/公共贡献资格；资格发生变化时丢弃过时摘要，重建当前规则榜单。不要在同步 dispatch 里阻塞等待模型，也不要每次切页自动产生付费请求。真实服务调用需另行验证。

## 验证入口

`care_person_snapshot` 覆盖精确来源、同名、冲突、角色、历史保留、关注加分/开关/排除和合成夹具。

`person_snapshot_server_tests` 使用真实本机客户端和服务器，验证权限先于目录调用、人物捕获、重试/暂停/异常、删除/重启后的足迹、时序 RPC 的关注/设置/反馈以及旧快照兼容和损坏检测。

`llm_trend_summary` 使用假模型服务器验证请求最小化、真实成功路径、精确大计数、超时/取消、坏响应、过大响应、重定向、无 Key 和回退。已有全量 CTest 与演示 E2E 继续运行。
