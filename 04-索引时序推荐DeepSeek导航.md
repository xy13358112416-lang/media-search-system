# 我的答辩代码导航：文件名、行号、函数名

更新时间：2026-11-18。按当前本地源码定位；后续源码改动后行号可能变化。点击文件链接跳转。表内写“函数内”的项目是代码段，不是独立函数。

## 我们负责的代码范围

按我们之前的分工，你重点讲：**索引检索、时序推荐、每日推荐与场景增强、DeepSeek仓库推荐、热榜统计及AI摘要、人物联网搜索、个人足迹统计**。

本导航只列我们的算法与模型核心代码，保留对应页面说明，方便实机答辩。已移除Qt控件、页面请求处理和服务器路由接线；账户、数据库、RPC不列入本人的讲解范围。文件放在server目录不代表都是别人的代码：person_profile_lookup.cpp是这里需要讲的人物模型处理模块。

## 1. 检索：建立索引与组合查询

检索页面。纯C++，不调用DeepSeek。

| 文件名与行号（点击） | 对应函数／位置 | 干什么 |
|---|---|---|
| [include/mediasearch.h:17](C:/Users/LENEVO/.codex/worktrees/review-integration-16-prs/media-search-system/include/mediasearch.h#L17) | `SearchQuery / MediaSearch` | 查询条件、记录表和倒排索引的数据结构。 |
| [retrieval/mediasearch.cpp:73](C:/Users/LENEVO/.codex/worktrees/review-integration-16-prs/media-search-system/retrieval/mediasearch.cpp#L73) | `MediaSearch::rebuild` | 完整建立索引，最后统一排序各倒排列表。 |
| [retrieval/mediasearch.cpp:102](C:/Users/LENEVO/.codex/worktrees/review-integration-16-prs/media-search-system/retrieval/mediasearch.cpp#L102) | `MediaSearch::indexRecord` | 建立导演／演员／文件类型→作品ID的关联。 |
| [retrieval/mediasearch.cpp:157](C:/Users/LENEVO/.codex/worktrees/review-integration-16-prs/media-search-system/retrieval/mediasearch.cpp#L157) | `MediaSearch::search` | 取生效条件的列表，短列表优先，使用有序交集得到匹配ID。 |
| [retrieval/mediasearch.cpp:125](C:/Users/LENEVO/.codex/worktrees/review-integration-16-prs/media-search-system/retrieval/mediasearch.cpp#L125) | `MediaSearch::addRecord` | 新增时维护索引。 |
| [retrieval/mediasearch.cpp:137](C:/Users/LENEVO/.codex/worktrees/review-integration-16-prs/media-search-system/retrieval/mediasearch.cpp#L137) | `MediaSearch::removeRecord` | 删除时维护索引。 |

## 2. 时序推荐：动态推荐的规则算法

动态推荐页面；默认今日推荐也从这条规则流程取得候选。纯C++，不调用DeepSeek。

| 文件名与行号（点击） | 对应函数／位置 | 干什么 |
|---|---|---|
| [include/care_temporal.h:20](C:/Users/LENEVO/.codex/worktrees/review-integration-16-prs/media-search-system/include/care_temporal.h#L20) | `TemporalScoringConfig` | 长期／近期半衰期及评分权重配置。 |
| [retrieval/care_temporal.cpp:172](C:/Users/LENEVO/.codex/worktrees/review-integration-16-prs/media-search-system/retrieval/care_temporal.cpp#L172) | `care_algorithms::recommendTemporal` | 核心入口：校验、建画像、评分、排序和类别多样性调整。 |
| [retrieval/care_temporal.cpp:85](C:/Users/LENEVO/.codex/worktrees/review-integration-16-prs/media-search-system/retrieval/care_temporal.cpp#L85) | `addView` | 按衰减权重累计类别和人物兴趣。 |
| [retrieval/care_temporal.cpp:116](C:/Users/LENEVO/.codex/worktrees/review-integration-16-prs/media-search-system/retrieval/care_temporal.cpp#L116) | `match` | 计算候选作品与兴趣画像的匹配分。 |
| [retrieval/care_temporal.cpp:218](C:/Users/LENEVO/.codex/worktrees/review-integration-16-prs/media-search-system/retrieval/care_temporal.cpp#L218) | `recommendTemporal 内：两次 addView` | 同一浏览分别使用长期、近期半衰期；不是把事件硬拆两堆。 |
| [retrieval/care_temporal.cpp:278](C:/Users/LENEVO/.codex/worktrees/review-integration-16-prs/media-search-system/retrieval/care_temporal.cpp#L278) | `recommendTemporal 内：候选评分循环` | 长期、近期、公共热度、新鲜度及偏好／关注奖励合成总分。 |
| [retrieval/care_temporal.cpp:298](C:/Users/LENEVO/.codex/worktrees/review-integration-16-prs/media-search-system/retrieval/care_temporal.cpp#L298) | `recommendTemporal 内：row.total 总分合成` | 四项加权：长期、近期、热度、新鲜度，再加类别、类型、关注三项奖励。 |
| [retrieval/care_temporal.cpp:315](C:/Users/LENEVO/.codex/worktrees/review-integration-16-prs/media-search-system/retrieval/care_temporal.cpp#L315) | `recommendTemporal 内：排序与结果选择` | 按分数排序，同分按稳定身份；尝试减少同类别连续出现。 |

## 3. 每日推荐：默认模式与非默认场景分支

今日推荐页面。默认模式用规则候选加日期抽取；放松／专注／探索增加DeepSeek联网场景选择。动态推荐也复用场景模型链路。

| 文件名与行号（点击） | 对应函数／位置 | 干什么 |
|---|---|---|
| [include/daily_pick.h:31](C:/Users/LENEVO/.codex/worktrees/review-integration-16-prs/media-search-system/include/daily_pick.h#L31) | `dailyPick` | 默认模式对候选按日期种子洗牌抽取；具体场景结果不走此抽取。 |
| [network/llm_scene.cpp:80](C:/Users/LENEVO/.codex/worktrees/review-integration-16-prs/media-search-system/network/llm_scene.cpp#L80) | `llm_scene::Client::recommend` | DeepSeek场景调用：联网核对候选作品信息，按场景选择候选ID；失败回退规则。 |
| [network/llm_scene.cpp:34](C:/Users/LENEVO/.codex/worktrees/review-integration-16-prs/media-search-system/network/llm_scene.cpp#L34) | `parse` | 校验搜索证据与模型返回的候选ID、数量。 |

## 4. DeepSeek资源仓库推荐：需求理解与简短推荐说明

资源仓库页面：输入自然语言需求，生成推荐。此链路不使用联网Search，也不是作品剧情简介生成。

| 文件名与行号（点击） | 对应函数／位置 | 干什么 |
|---|---|---|
| [network/llm_candidate_recall.cpp:84](C:/Users/LENEVO/.codex/worktrees/review-integration-16-prs/media-search-system/network/llm_candidate_recall.cpp#L84) | `llm_recommendation::selectCandidates` | 词面评分和排序，相关优先、类别补位，候选最多30条。 |
| [network/llm_candidate_recall.cpp:67](C:/Users/LENEVO/.codex/worktrees/review-integration-16-prs/media-search-system/network/llm_candidate_recall.cpp#L67) | `score` | 标题／人物／类别取最高匹配信号。 |
| [network/llm_candidate_recall.cpp:56](C:/Users/LENEVO/.codex/worktrees/review-integration-16-prs/media-search-system/network/llm_candidate_recall.cpp#L56) | `fieldScore` | 单字段按完整相等、短语包含、词项命中打分。 |
| [network/llm_recommendation.cpp:151](C:/Users/LENEVO/.codex/worktrees/review-integration-16-prs/media-search-system/network/llm_recommendation.cpp#L151) | `llm_recommendation::Client::recommend` | 建立Job、保存completion、组织JSON、异步POST并处理超时。 |
| [network/llm_recommendation.cpp:54](C:/Users/LENEVO/.codex/worktrees/review-integration-16-prs/media-search-system/network/llm_recommendation.cpp#L54) | `parse` | 解析模型返回，检查候选ID、重复项、理由与limit。 |
| [network/llm_recommendation.cpp:28](C:/Users/LENEVO/.codex/worktrees/review-integration-16-prs/media-search-system/network/llm_recommendation.cpp#L28) | `fallback` | 请求或格式失败时使用规则推荐；合法空结果不强行填满。 |
| [network/llm_recommendation.cpp:141](C:/Users/LENEVO/.codex/worktrees/review-integration-16-prs/media-search-system/network/llm_recommendation.cpp#L141) | `Client::complete` | 统一结束任务并通过保存的completion交付结果。 |

## 5. 全站热榜：统计与排名

全站热榜页面的作品榜／搜索词榜。纯C++聚合统计；与下面的AI摘要分开。

| 文件名与行号（点击） | 对应函数／位置 | 干什么 |
|---|---|---|
| [retrieval/care_trending.cpp:51](C:/Users/LENEVO/.codex/worktrees/review-integration-16-prs/media-search-system/retrieval/care_trending.cpp#L51) | `care_algorithms::buildTrending` | 按窗口与榜单类型筛选、聚合、三人门槛、排序、取前limit。 |
| [retrieval/care_trending.cpp:27](C:/Users/LENEVO/.codex/worktrees/review-integration-16-prs/media-search-system/retrieval/care_trending.cpp#L27) | `Contributions::add` | 同用户、同对象、同一天最多贡献一次有效行为。 |
| [retrieval/care_trending.cpp:110](C:/Users/LENEVO/.codex/worktrees/review-integration-16-prs/media-search-system/retrieval/care_trending.cpp#L110) | `buildTrending 内：排序` | 按不同用户数、有效行为数降序，再按稳定键。 |

## 6. DeepSeek AI摘要：解释已统计好的热榜

全站热榜页面→生成 AI 摘要。模型负责文字，不计算热度、不改排名或精确计数。

| 文件名与行号（点击） | 对应函数／位置 | 干什么 |
|---|---|---|
| [network/llm_trend_summary.cpp:163](C:/Users/LENEVO/.codex/worktrees/review-integration-16-prs/media-search-system/network/llm_trend_summary.cpp#L163) | `llm_trend_summary::Client::summarize` | 组织统计证据、请求模型、解析并检查摘要；失败返回规则摘要。 |
| [network/llm_trend_summary.cpp:51](C:/Users/LENEVO/.codex/worktrees/review-integration-16-prs/media-search-system/network/llm_trend_summary.cpp#L51) | `aggregateRows` | 把公开热榜整理成模型输入行，保留精确统计证据。 |
| [network/llm_trend_summary.cpp:85](C:/Users/LENEVO/.codex/worktrees/review-integration-16-prs/media-search-system/network/llm_trend_summary.cpp#L85) | `parseSummary` | 校验模型摘要及其引用条目，精确计数由代码添加。 |

## 7. DeepSeek联网搜索：人物主页与代表作品

关注中心／人物与作品→联网人物主页。本地“查找人物”不是模型搜索。外部作品是参考资料，不自动入库或变成可播放资源。

| 文件名与行号（点击） | 对应函数／位置 | 干什么 |
|---|---|---|
| [server/person_profile_lookup.cpp:173](C:/Users/LENEVO/.codex/worktrees/review-integration-16-prs/media-search-system/server/person_profile_lookup.cpp#L173) | `person_profile_lookup::lookup` | 用DeepSeek集成Search整理人物、简介、代表作品和来源。 |
| [server/person_profile_lookup.cpp:84](C:/Users/LENEVO/.codex/worktrees/review-integration-16-prs/media-search-system/server/person_profile_lookup.cpp#L84) | `parseReply` | 解析人物／作品结构，核对引用URL属于搜索来源。 |

## 8. 个人足迹：本人行为统计

个人足迹页面。你前面确认负责的统计部分，保留在此；不调用DeepSeek，也不负责推荐排序。

| 文件名与行号（点击） | 对应函数／位置 | 干什么 |
|---|---|---|
| [retrieval/care_footprint.cpp:130](C:/Users/LENEVO/.codex/worktrees/review-integration-16-prs/media-search-system/retrieval/care_footprint.cpp#L130) | `care_algorithms::buildFootprint` | 校验本人快照和时间区间，生成主区间与可选比较区间报告。 |
| [retrieval/care_footprint.cpp:50](C:/Users/LENEVO/.codex/worktrees/review-integration-16-prs/media-search-system/retrieval/care_footprint.cpp#L50) | `summarize` | 自然日补零，统计浏览、搜索、不同作品、类别、人物和活跃日。 |
| [retrieval/care_footprint.cpp:27](C:/Users/LENEVO/.codex/worktrees/review-integration-16-prs/media-search-system/retrieval/care_footprint.cpp#L27) | `localDate` | UTC秒转换为指定时区日期，足迹使用北京时间。 |
| [retrieval/care_footprint.cpp:35](C:/Users/LENEVO/.codex/worktrees/review-integration-16-prs/media-search-system/retrieval/care_footprint.cpp#L35) | `calendarWindow` | 计算左闭右开区间覆盖的自然日。 |

## 演示时最容易混的区别

- **时序推荐**：我们的C++规则算兴趣与分数。
- **默认每日推荐**：规则候选加日期种子抽取。
- **非默认场景每日推荐**：规则候选再交DeepSeek按场景选择；失败回退规则。
- **仓库推荐**：文字需求→粗筛候选→模型选择并写推荐说明。
- **全站热榜统计**：计数、公开门槛和排名由我们的C++完成。
- **热榜AI摘要**：模型解释既有统计，不能改计数。
- **联网人物搜索**：外部人物与作品资料，不是本地媒体检索。
- **个人足迹**：总结本人历史，不是给下一次推荐排序。

旧的Search→Fetch人物资讯缓存后台不列入当前演示主线；旧资讯／发布日历入口已移除，不能把它当成已接通的第五个页面功能。

