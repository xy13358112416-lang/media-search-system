# 人物数据表迁移草案

此变更只定义服务器同一份 SQLite 数据库内的人物表和迁移，不提供人物读写 RPC、资讯抓取或历史事件快照写入。表结构及版本号需要与正在制定的 N00 仓储边界共同审查；草案未合并前，不应把它当作已经确认的公共协议。

`WarehouseServer::openDatabase` 在现有初始化事务中执行 `person_schema::migrate`：从版本 1 直接创建当前版本 3 的人物表并登记版本 2、3；版本 2 会先校验既有结构，再无损增加 `refresh_generation` 并登记版本 3；任何步骤失败由调用方回滚。已经是版本 3 时检查字段、类型、主外键、索引及必要 CHECK 片段；CHECK 检查不是完整 SQL 语义分析。不支持的后续版本、结构不匹配或未开启外键时明确失败。之后修改表结构必须使用新的迁移版本，不直接修改版本 3 的既有定义。迁移不从旧姓名字段推断人物 ID，也不改动媒体、账号、历史或反馈内容。

| 表 | 保存的内容 |
|---|---|
| `person_catalog`、`person_aliases` | 已确认的稳定人物 ID、显示名、身份来源和别名；相同姓名不能自动合并 |
| `person_credits` | 人物 ID、服务端作品 ID、角色及对应来源；每个角色独立记录 |
| `person_follows` | 用户自己的关注，以及关注时已知的最小姓名/角色快照；作品失效后可供后续仓储实现安全取关 |
| `person_sources`、`person_updates` | 已配置资讯源和有来源的缓存记录；`person_sources.source_url` 是服务端配置的 feed 身份，`person_updates.feed_url` 引用它，文章自身的 `person_updates.source_url` 是另一个字段。`refresh_generation` 是跨连接/进程持久化的递增刷新代次。未知日期可以为空，失败可标记 stale，不伪造抓取时间 |

人物历史快照唯一存放在既有 `behavior_events.media_snapshot` 的 `_care_people_v1` 字段，由主分支的行为采集和快照解码负责；清理行为记录时一并清理快照。旧浏览没有该字段时不根据当前目录补造人物。数据库连接仍由服务器持有，迁移函数不创建连接、不自行提交。后续 N03 仓储负责校验身份、HTTPS 来源、内容、时间和权限；Qt 的服务端查询与资讯 provider 通过仓储接口访问，不直接创建另一份数据库。

自动测试覆盖旧用户/行为保留、重复迁移、版本 2 无损升级、表名与索引冲突时回滚、外键关闭、未知及超大版本号、已有结构被改动；还需与实际 Qt 端确定的 N00 接口共同验收。

## 仓储与资讯缓存接口

`person_store::Store` 是服务器内部接口，不是网络 RPC，也不包含人物/作品导入：

- `contextFor` 只接受认证用户和服务端已登记、当前可读的精确 `(workId, warehouseId, itemId)`；未知人物关联返回空列表，数据库或权限失败返回错误。
- `followsFor` 只读取当前用户自己的分页关注；`setFollow` 在事务内重新检查当前可读关联，取关允许在来源撤权或删除后执行。关注快照只包含该用户当时可见的角色。
- `updatesFor` 先检查当前用户是否仍能读取该人物。没有缓存时，只有所有相关 feed 至少成功刷新过一次才返回成功的空列表；首次失败、失败后无旧缓存、缓存损坏或数据库错误均返回错误。
- `beginUpdatesRefresh(personId, feedUrl)` 返回持久化递增代次并把该 feed 标为 stale。provider 必须在每个新刷新开始时调用一次。
- `failUpdatesRefresh(personId, feedUrl, generation)` 只在代次仍为当前时标记失败并保留旧缓存；旧代次不能覆盖新刷新的状态。
- `mergeUpdates(personId, feedUrl, generation, completedAtUtc, updates)` 先在事务内校验当前代次，再验证全部更新并原子替换该 feed 的缓存。迟到代次返回 `InvalidArgument`；验证或写入失败会保留旧缓存并标为 stale。

`feedUrl` 必须等于服务端已配置的 `person_sources.source_url`。它绝不能从 `PersonUpdate.sourceUrl` 推断，后者只表示具体资讯文章。每条缓存记录也必须保持 `update.personId == personId`、HTTPS 文章地址、合法日期和 `fetchedAtUtc <= completedAtUtc`。
