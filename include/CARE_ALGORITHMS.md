# 关怀算法模块对接说明（R07 / R09 / R08 / R10）

这些是服务器调用的同步纯函数；不打开 SQLite、不读 CSV、不发网络请求、不持有会话。现有本机账户、检索与推荐接口保持不变。网络 `CareService`、数据查询与 Qt 页面仍由②、①实现。基础规则见 `CARE_CONTRACT.md`。

## ②怎样调用

1. 从服务器会话取得用户，查询当前授权目录、行为、关注、反馈和参与设置，固定一次 `nowUtc`。
2. 构造 `care_algorithms` 的快照，调用下表函数。
3. 处理 `Result.status`；成功时 `value` 存在，包括合法空结果。数据库失败设置 `historyAvailable/dataAvailable=false`，不能替换成空列表冒充新用户。
4. 通过网络返回前再次检查会话和每条资源来源的当前可读性。算法的结构校验不替代认证或授权。

| 头文件 / CMake target | 入口 | 输入范围 |
|---|---|---|
| `care_person.h` / `care_person` | `searchPeople`、`worksForPerson` | 当前用户可读作品和主动关注集合 |
| 同上 | `suggestPeople` | 本人的历史与当前可读作品；只返回建议，不创建关注 |
| 同上 | `personUpdates` | 已获取的带来源缓存；不在这里抓取新闻 |
| `care_footprint.h` / `care_footprint` | `buildFootprint` | 本人的历史；可含已删除作品的最小历史快照 |
| `care_trending.h` / `care_trending` | `buildTrending` | 有效公共贡献行为和当前公开来源，不能传全部可读私有作品 |
| `care_temporal.h` / `care_temporal` | `recommendTemporal` | 已授权/召回的当前候选、本人的历史、可信公共热度、主动关注和排除反馈 |

所有 target 由 `network/CMakeLists.txt` 引入，服务器按需链接；Qt 前端无需直接链接这些模块。`care_algorithm_core` 是共用实现细节，不是网络服务接口。

## 快照必须表达的事实

- `ObservedBehavior.userKey` 是服务器认定的稳定身份。事件时间为可信 UTC 秒；正常客户端不能传历史时间、身份或 `synthetic`。重复事件按 `(userKey,eventId)` 去重；相同 ID 的内容/时间冲突返回错误。
- `workSnapshot` 可省略。存在时代表当时保存的一整份最小元数据，不混入今天的类别或人物；不存在时足迹/推荐可按稳定作品 ID 查当前元数据；再找不到时足迹仍计浏览次数和“未知”类别，推荐不虚构特征。
- `readableWorks` 只放当前可读来源。同一作品多个来源按稳定作品 ID 去重，选择来源 ID 最小的真实可读位置；同一来源位置的冲突元数据报错。
- `publicEligible` 由服务器确认行为发生时及当前的公共贡献资格。`publicWorks` 只放当前公开来源；来源转私有、撤销公共贡献和清理历史后，②必须更新查询/缓存，不能继续传旧资格。
- `publicHeat` 是所选公开时间窗口内，按同用户/作品/北京时间自然日封顶、达到三用户门槛的有效计数。②应提供完整窗口统计，不能把媒体 CSV 的累计 `viewCount` 当它使用，也不要直接拿截断的 UI 榜单充当完整统计。`publicHeatContainsSyntheticData` 必须保留统计来源标记。
- 人物必须有服务端目录确认的 `personId`。姓名不构成身份，算法不会把同名演员/导演自动合并。原始 CSV 中只有姓名时，关联未确认的 `people` 应为空。

## 各模块行为

人物搜索对当前可见别名做 NFKC、完整 case-fold 和子串匹配，按人物 ID 稳定分页；同一人物的多种角色合并。人物建议按浏览次数、最近时间、人物 ID 排序，排除已关注和当前不可见人物。更新缓存按同一 updateId 最新 fetchedAt 版本选取，再按时间窗口过滤；同版本冲突报错。来源必须是无账号信息的 HTTPS 地址，发布日期不可晚于现在，已确认的未来上映日允许保留。

热榜先过滤范围/公共资格，再规范化词语，按用户/对象/北京时间自然日各计一次，至少三名不同用户才公布。排序为独立用户数、有效次数降序及稳定键升序。默认摘要仅解释现有计数，`summaryFromModel=false`；未实现 DeepSeek 摘要接线，也不需要它才能返回榜单。

足迹区间为 `[begin,end)`，北京时间自然日补零，每个区间最多覆盖 3660 天。原始次数、不同作品、活跃天、类别、人物角色分别统计；人物次数之和可能超过浏览次数。coverageStart 来自实际可用历史，不代表所选整年都有记录。对比区间明确传入，不把点击次数转成播放时长。

时序推荐使用 R00-v1 默认参数：长期/近期按每条浏览事件分别衰减（半衰期 90/7 天），基础权重为 0.35/0.40/0.15/0.10，类别/人物匹配权重为 0.6/0.4。热度 log1p 归一化，可信发行日的新鲜度半衰期 30 天；未知/未来发行日为零。实验参数集中在 `TemporalScoringConfig`，调用方应记录实验版本。

Auto 类别使用历史；Any/Explicit 不从历史补类别，但人物兴趣独立保留。文件类型 Auto 在 R00-v1 中没有隐式类型得分，只有 Explicit 软加分。关闭个性化排除隐式历史和关注加分；不感兴趣仍为硬排除。无个人特征时由可用热度和新鲜度重新归一化，全零按稳定 ID 排序。有正相关候选时不拿零分项凑数；有其他正分类别时避免连续三项同类。`scene` 第一版只校验，不从点击猜测情绪或作品场景。

推荐理由列出各分量、实际权重和总分，便于联调和答辩核对。长期、近期和公共热度共同作用，最近两周兴趣改变不保证首推必然改变。`containsSyntheticData` 对使用的模拟行为/热度保留标记；不能将模拟实验解释为真实用户满意度提升。

## 验证入口

项目 Release 构建后运行 `ctest --test-dir build --output-on-failure`。纯模块测试名为 `care_algorithm_common`、`care_person`、`care_footprint`、`care_trending`、`care_temporal`。有 Python 3 时还会运行 `care_simulation_generator` 和 `care_simulation_algorithms`；生成数据、离线报告和消融示例见 `sample-data/simulation/README.md`。

时区使用 `Asia/Shanghai`。若平台的 Qt 时区数据没有 1988 年历史夏令时，历史转换专用测试明确跳过；当代跨日边界仍测试，不以 UTC+8 常数替代算法中的时区处理。
