# R00：五项关怀功能共享契约 v1

这是①Qt、②网络/存储、③检索推荐的共同开发规则。头文件为`behavior_contract.h`、`care_contract.h`，已有仓库协议以`warehouse_contract.h`为准。R00仅交付类型、方法声明、结构校验、操作名和消费测试；数据库、网络客户端、页面及推荐实现由后续PR完成。

## 1. 运行位置与权限

新五项在线功能使用同一个服务器网络会话。`CareService`与`WarehouseService`的实现共享连接、token和请求表。客户端不传用户ID，服务器从连接内token取得身份。所有接口（包括公共榜单读取）都要求有效网络登录。

①负责交互与展示，②负责认证、查询、事务、传输、数据源缓存和模块装配，③负责纯计算、召回与推荐。③算法传入已授权快照及固定`nowUtc`，不直接依赖本机账户服务或SQLite连接。原本机CSV/账户/检索继续离线工作，未共享资源的本机历史不通过此接口上传。

Key仅服务器读取`MEDIA_SERVER_DEEPSEEK_API_KEY`或服务器私有配置。模型只接收已授权候选和必要主题，不发送token、账号密码或完整个人行为历史。客户端和自动化测试均不需要真实Key。

`validate(...)`只做字段边界检查；成功绝不意味着身份已验证、资源存在或有权访问。服务端在执行任何操作前认证、在读写前授权；推荐返回前再次检查会话及每个来源条目。数据库故障映射`ServerError`，不能伪装无历史或触发无Key回退。

## 2. 身份、资源与人物标识

- `ResourceRef.workId`是稳定的作品标识，例如`uci:AA13`，与客户端某个CSV的本地行编号区分。服务器维护目录映射，不能盲信客户端提交的作品ID。
- `ResourceRef.source={warehouseId,itemId}`是实际来源位置；两者是正qint64。服务器必须验证该位置的作品与workId匹配、当前可读，不能凭公开作品ID读取别人的私有仓库。
- 同一作品的多个可读来源可去重，但返回必须保留一个当前可读的真实来源；不能移花接木到不可读位置。
- ResourceSummary始终带ResourceRef和关联people；MediaRecord.id仅是元数据快照的原编号，不替代workId。旧仓库/旧推荐条目通过getResource(source)解析稳定身份，新动态推荐直接返回RecommendedResource{work,reason}，客户端不用猜作品ID。
- `personId`由服务器人物目录分配/映射，名字是展示字段。同名不自动合并；同一人物可以有Actor/Director/Musician多种角色。
- 首次关注可通过searchPeople(query,page)取得PersonSummary，或从ResourceSummary.people取得关联人物；人物搜索只返回至少有一个当前可读关联作品的人物，名字匹配统一NFKC/case-fold后子串匹配，按personId升序分页。查询不能为空；取消自己的既有关注不受新搜索可见性限制。
- 不透明标识（事件ID、作品ID、人物ID、更新ID）最多128个UTF-16代码单元，不含空白、NUL或控制字符。JSON中的数据库ID、计数、offset和UTC秒均使用十进制字符串，禁止double中转。

## 3. 时间、分页与空结果

- `TimeRange`是Unix秒级UTC区间`[beginUtc,endUtc)`；起点包含、终点不包含。起点非负，终点大于起点且不超过253402300800。毫秒值应被拒绝。
- 自然日、周、月、年按`Asia/Shanghai`生成边界再转UTC；自然周从周一00:00开始。滑动7/30天窗口与自然周/月在UI上区分。
- `FootprintRequest.comparison`可省略；提供时必须完全位于当前区间之前。前后自然月长度不同，UI展示原始计数时注明区间，比较频率则按实际天数归一化。
- 分页默认offset=0、limit=20，limit范围1—50。`Page.nextOffset`不存在表示结束；稳定排序后分页。关注按人物ID升序，作品按workId/来源ID升序，更新按eventAt或publishedAt降序、updateId升序；无日期更新排最后。
- 空数据是`Status::None`和实际空结果，不能拿默认构造的`Result`冒充成功：成功时`value`必须存在，失败时不存在。
- 推荐无匹配时正常返回空items及message，`fallback=false`；HTTP/服务不可用的规则回退明确`fallback=true`。模型理由及外部文字用纯文本展示。

## 4. 行为记录与个人控制

`BehaviorEvent`没有userId、时间或synthetic字段。View必须有完整资源引用，query为空；Search必须有非空query（最多1000个UTF-16单元），resource不存在。正常服务端以接收UTC时间入库；历史时间只能由独立演示导入工具写入。

- 只有主动打开在线媒体详情或明确提交的在线公开目录搜索计事件。自动选中、换页、刷新、每次按键不计事件；一次用户操作只能由一个控制器上报。
- `(认证用户,eventId)`唯一。相同ID及相同业务内容重试是幂等成功，不重复计数；相同ID但不同内容返回InvalidArgument。每次真实再次操作使用新ID。
- `ParticipationSettings`默认historyEnabled=true、personalizationEnabled=true、contributePublicTrends=false。公开统计需用户主动启用。
- historyEnabled=false时接受结构合法的事件但不持久化、不更新个人或公共派生统计；返回成功并可用message说明暂停。关闭开关不自动删除旧记录。
- personalizationEnabled=false时不使用隐式历史或关注推断的兴趣评分；仍尊重当前显式类别/类型/场景选择和不感兴趣反馈。足迹可继续统计已有历史。
- 公共贡献关闭时撤销已有事件的公共贡献资格并使公共派生缓存失效；不把此前私人搜索突然公开。再次打开只对打开后新事件生效，旧事件不恢复资格。
- clearHistory删除当前用户行为及其派生画像、公共贡献与相关缓存；保留明确关注关系和显式不感兴趣反馈。操作事务完成后返回，不能只清表格。
- `WorkFeedbackKind::NotInterested`表示按workId排除该作品，跨来源也生效；Neutral撤销反馈。设置需要验证来源可读；撤销自己已有反馈即使来源刚失效也应允许。反馈列表只返回当前可读资源，隐去失效来源。
- 旧累计历史无法还原逐日事件，只能作为标明来源的累计兼容数据，不伪造成某日点击。历史作品消失时足迹可显示已存的最小统计快照；新推荐必须排除失效来源。

## 5. 五项功能的固定口径

### 人物关注与更新

关注关系由用户明确设置，重复关注/取消幂等；浏览可产生“可能关注”的建议，但不自动关注。取消自己已有关注即使人物不再有可读作品也应允许。关联作品按当前权限过滤。新闻和日历取有来源的结构化数据，必须保存sourceUrl与fetchedAt；日期未知不生成日历项，来源失效可展示缓存和stale标志。模型不得编造新作、上映时间或来源链接。

### 公共热榜

只统计用户明确启用公共贡献后发生的公开目录行为；资源当前仍须公开可读。Search词先NFKC、去首尾空白、合并连续空白、case-fold后聚合，私人过滤条件不纳入热词。至少3名不同贡献用户的条目才公开；每用户、每作品/规范化词、每北京时间自然日最多贡献1次有效事件。

排序先distinctUsers降序，再effectiveEvents降序，最后稳定键升序。窗口结束后旧行为自然退出；同一个人反复点击不能把独立用户计数刷大。摘要仅使用当前可公开榜单的字段/计数，失败仍返回原始榜单，summaryFromModel=false。

### 温暖模式

Neutral/Relax/Focus/Explore为用户主动选择，默认Neutral；动效开关保存在客户端设置。场景可撤销，只在检索/推荐规则有明确标签映射时影响结果，否则仅改变展示。不能声称从点击诊断情绪。

### 个人媒体足迹（功能4）

PeriodFootprint的views/searches是去重事件后的原始次数；uniqueWorks按workId去重；activeDays是任意view/search发生过的北京时间自然日数量。每次view按当时保存的主类别计1次，缺类别归“未知”；人物按每次事件中不同(personId,role)各计1次，因此人物次数之和可大于views。days按localDate升序，补齐区间覆盖自然日的0计数。

报告返回实际覆盖起点coverageStartUtc，没有历史则不存在；只有90天记录不能伪称完整年度消费。没有播放事件时不输出观看时长、完播率或观看完成数。当前统计与comparison各自独立计算；算法不读取推荐结果制造画像。

### 时序动态推荐（功能5）

数量范围1—30，默认5。复用Auto/Any/Explicit：Auto从历史推断；Any不补该字段；Explicit使用明确值。推荐偏好为软加分，权限和不感兴趣为硬约束。当前主题推荐recommend_resources保留原1—50请求契约，实际模块可少返回；不修改它的旧签名。

第一版配置基准：每个view分别计算`2^(-ageDays/90)`长期权重、`2^(-ageDays/7)`近期权重；未来事件不参与，不能以累计次数乘最后一次时间。两种兴趣各自归一化，作品匹配使用类别0.6、人物0.4，无人物特征时重分配给类别。

基础分为长期0.35＋近期0.40＋公共热度0.15＋新鲜度0.10，各分量先归一化到[0,1]。热度采用当前可公开窗口有效贡献的`log1p(count)/log1p(maxCount)`；新鲜度取可信发行日距now的30天半衰期，未知或未来发行日为0。无个人兴趣/关闭个性化时，个人兴趣两项退出，由可用热度与新鲜度重新归一化；全零则按稳定作品ID排序。

显式类别匹配额外加0.20，显式类型匹配加0.10，不因未知类型捏造扩展名；启用个性化且匹配主动关注人物额外加0.05。最终分可超过1。场景第一版只对已确认标签作同分重排，不自动推断作品标签。相同分按workId和来源ID稳定排序；多样性重排在存在其他合格类别时避免连续超过2项同类，不能填入无关候选凑数量。参数集中配置，实验记录版本，调参通过独立算法PR。

候选先做标题/人物相关召回与类别覆盖，解决500条截30遗漏，再评分；给DeepSeek的候选仍不超过30。DeepSeek可用于语义筛选和理由，不独占可用路径。history读取失败是ServerError，不是冷启动；合法无匹配不补热榜。

## 6. C++服务入口与网络操作

`CareService`由②实现，①使用；③实现纯算法。所有回调在所属线程恰好一次，可立即失败同步回调；调用者不能假设句柄赋值早于回调。recommendTemporal返回与已有仓库推荐同类型的不透明句柄；两个服务实现共用同一连接请求表，句柄不互相冲突。

| 操作/方法 | 请求payload | 成功payload |
|---|---|---|
| record_behavior / recordBehavior | event | 空对象 |
| get_participation_settings / getParticipationSettings | 空对象 | settings |
| set_participation_settings / setParticipationSettings | settings | 空对象 |
| clear_history / clearHistory | 空对象 | 空对象 |
| get_resource / getResource | source | work（ResourceSummary） |
| search_people / searchPeople | query,offset,limit | page（PersonSummary） |
| list_work_feedback / listWorkFeedback | offset,limit | page（WorkFeedback） |
| set_work_feedback / setWorkFeedback | resource,feedback | 空对象 |
| list_follows / listFollows | offset,limit | page（PersonSummary） |
| set_follow / setFollow | person_id,followed布尔值 | 空对象 |
| get_person_works / getPersonWorks | person_id,offset,limit | page（ResourceSummary） |
| get_person_updates / getPersonUpdates | person_id,range,offset,limit | page（PersonUpdate） |
| get_trending / getTrending | kind,range,limit整数,summary_mode可省略 | result（TrendingResult） |
| get_footprint / getFootprint | range,comparison可省略 | report（FootprintReport） |
| recommend_temporal / recommendTemporal | category,file_type,scene,limit整数 | result（TemporalRecommendationResult） |

分页请求中的 `offset` 是十进制字符串、`limit` 是整数，两者直接位于请求 `payload`，不再额外嵌套 `page`。例如 `search_people` 的 `payload` 为 `{"query":"名字","offset":"0","limit":20}`。分页响应仍以 `page` 对象返回。此约定与已实现的 `list_work_feedback` 请求保持一致；新增人物 RPC 及资讯客户端应按此格式实现并参与联调。

沿用4字节长度前缀与JSON `{type,req_id,token,payload}`。响应type为请求type＋`_resp`，错误使用现有error_resp/Status。cancel_recommendation复用于recommend_resources、recommend_temporal及模型模式get_trending，target_req_id作用域为同一连接；本地先以Cancelled完成原回调，取消确认不再次调用原回调。未知操作在当前未实现服务端被拒绝，不返回伪造业务成功。

`TrendingRequest.summaryMode` 为 `TrendingSummaryMode::Rules` / `Model`，默认 Rules，追加在既有字段末尾。网络 `summary_mode` 只能为字符串 `rules` 或 `model`；省略表示 rules，null 或其他值无效。普通切页/筛选/刷新只请求规则摘要，用户主动点击“生成 AI 摘要”才请求 model。模型模式复用较长的推荐请求超时，超时和退出会话时内部发送取消；`getTrending` 的公开回调接口保持不变，不新增取消句柄。

模型在服务器异步运行，只接收符合公开门槛的聚合榜单。发送前重新核对会话并重新聚合；若条目、计数或公开资格影响了结果，丢弃旧模型文本并返回当前规则榜单，不自动再调用模型。模型任务使用独立记录，取消不会误取消仓库推荐。无 Key、超时或模型失败时正常返回规则摘要，`summary_from_model=false`。

## 7. JSON字段映射（供后续编解码实现）

所有C++ camelCase字段转snake_case；禁止额外信任客户端user_id、occurred_at_utc、synthetic。事件只接收BehaviorEvent的四个字段，来源或搜索字段按动作二选一。忽略其余未知兼容字段不得使其参与授权或时间赋值。

- ResourceRef：`work_id`字符串、`source:{warehouse_id,item_id}`正十进制字符串。ResourceSummary：`resource`、现有media_json定义的`media`、`people`人物摘要数组；没有已确认关联时people为空数组。
- BehaviorEvent：`event_id`、`action`（view/search）、`resource`可省略、`query`（view为空字符串）。缺省空query允许view；search必须提供非空query。
- ParticipationSettings：`history_enabled`、`personalization_enabled`、`contribute_public_trends`均为布尔值；更新必须提供全部三项，不把缺字段默认为false。
- TimeRange：`begin_utc,end_utc`十进制字符串；PageRequest：`offset`十进制字符串、`limit`JSON整数。Page：`items`数组、`next_offset`可省略（有则十进制字符串）。
- PersonSummary：`person_id,name,roles,followed`，roles为actor/director/musician字符串数组。PersonUpdate：头文件全部字段转snake_case；kind为news/release，日期可省略，source_url为绝对HTTPS URL，stale布尔。
- WorkFeedback：`work`为ResourceSummary、`feedback`为neutral/not_interested。设置请求使用`resource`而非work，不允许客户端提供完整作品覆盖服务器记录。
- TrendingResult：kind为works/search_terms；range对象、items数组、generated_at_utc、summary字符串、summary_from_model和contains_synthetic_data布尔值。TrendingItem：work可省略、term字符串、distinct_users/effective_events非负十进制字符串；两种条目不混用。
- FootprintReport：current对象、comparison可省略、generated_at_utc、message和contains_synthetic_data。PeriodFootprint：range、coverage_start_utc可省略、views/searches/unique_works/active_days为非负十进制字符串、categories/people/days数组。CountBucket为key/label/count；PersonCount为person_id/name/role/count；DailyActivity为local_date/views/searches。
- TemporalRecommendationRequest：category/file_type为`{mode:"auto|any|explicit",value:"..."}`，scene为neutral/relax/focus/explore，limit JSON整数。Auto/Any的value忽略。响应items为RecommendedResource数组，每项`{work:ResourceSummary,reason:字符串}`；engine为temporal/deepseek，另有message、fallback、generated_at_utc、contains_synthetic_data。已有recommend_resources的旧响应格式不变。
- optional字段写出时省略，读取接受省略或JSON null；非optional必填字段不能用null替代。所有时间和大计数都为字符串，limit只接受有限JSON整数并检查范围。

示例：

```json
{"type":"record_behavior","req_id":"21","token":"session-token","payload":{"event":{"event_id":"demo-event-1","action":"view","resource":{"work_id":"uci:AA13","source":{"warehouse_id":"1","item_id":"2"}},"query":""}}}
{"type":"get_footprint","req_id":"22","token":"session-token","payload":{"range":{"begin_utc":"1761926400","end_utc":"1764518400"}}}
{"type":"recommend_temporal","req_id":"23","token":"session-token","payload":{"category":{"mode":"auto","value":""},"file_type":{"mode":"any","value":""},"scene":"relax","limit":5}}
```

服务器后续测试必须覆盖真实鉴权、字段类型、幂等、迁移、取消、统计与算法。R00的结构校验测试不代表这些服务已经实现。

## 8. 紧接本PR的分工

- ②按本契约实现R01网络客户端、R03模型桥接、R05事件持久化和查询；server网络用户表仍为network_users。避免把新方法加进旧WarehouseService导致无关消费者都必须重写，新增CareService可由同一个客户端实现两接口。
- ③完成R02召回/query适配；再独立完成R09足迹、R08热榜、R10时序评分及对应单测，不把功能4/5合成一个接口。
- ①以CareService假实现先做页面，之后接真实客户端。用户事件只由一个上报控制器触发；回调/切账号/切页按已有取消与代次规则处理。
- 演示导入器把20个user_ref和作品ID映射到服务器ID，6420事件重复导入不累加；synthetic由服务器导入工具标记，正常客户端不能指定。
