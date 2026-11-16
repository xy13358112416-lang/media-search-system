# 可信人物资讯源配置

`person_source_config` 是服务器内部配置模块，不提供网络 RPC。Qt 和其他客户端不能提交抓取 URL；feed 也不会从人物主页、作品证据或文章地址自动推断。正式操作应先停止服务器并备份数据库。

## 配置格式

配置是服务器本地 JSON 文件，格式为 `care-person-source-config-v1`：

```json
{
  "schema": "care-person-source-config-v1",
  "provenance": "Operations reviewed the official feed and recorded its source page.",
  "sources": [
    {
      "person_id": "person:example",
      "feed_url": "https://feeds.example.org/people/example"
    }
  ]
}
```

- 根对象只允许 `schema`、`provenance`、`sources`。
- `schema` 固定为 `care-person-source-config-v1`。
- `provenance` 是操作人员对核验过程的说明，不能为空，最长 1000 个 UTF-16 单元。
- `sources` 最多 10000 项；配置文件最大 2 MiB。
- `person_id` 必须是数据库中已经存在的人物稳定 ID。
- `feed_url` 必须是绝对 HTTPS URL，必须有主机，不能包含用户名、密码或 fragment，最长 2048 个字符。
- JSON 使用 snake_case；额外字段、错误类型和不合法地址会直接报错，不做猜测修正。

样例见 `sample-data/person/person-sources.example.json`。样例中的 ID 和 `example.org` 地址均为占位内容，不能直接用于正式库。

## 登记语义

登记是追加式且幂等的：

- 新增来源时写入 `person_sources`，`last_success_utc` 保持 `NULL`，`stale=0`，`refresh_generation=0`，不会伪造成功抓取状态。
- 已存在相同 `(person_id, source_url)` 时只计入 `unchanged`，不修改旧行的成功时间、stale 或刷新代次。
- 一个人物可以登记多个 feed；同一 feed URL 也可以服务多个已确认人物。
- 整批在 SAVEPOINT 中执行。人物不存在、URL 错误、重复冲突或数据库失败时回滚本批全部写入，不影响调用方早先事务。
- 本版本没有修改或删除入口。配置文件中不再列出的旧来源不会被删除、停用或清空缓存；要删除来源必须另做显式迁移/运维工具，并先明确旧资讯缓存的处理方式。给人物更换 feed 时应追加新来源，不能静默覆盖旧 feed。

## 命令行

先使用 `media_person_import` 导入人物目录，再登记 feed：

```powershell
& .\media_person_source_config.exe `
  --db D:\demo\server.sqlite `
  --config D:\demo\person-sources.json
```

成功时输出：

```json
{"inserted":1,"unchanged":0}
```

再次执行相同配置输出 `{"inserted":0,"unchanged":1}`。工具不下载 feed，也不修改资讯缓存。

## 程序接口

头文件为 `server/person_source_config.h`：

```cpp
namespace person_source_config {
struct Entry { QString personId; QUrl feedUrl; };
struct Batch { QString provenance; QList<Entry> entries; };
struct Report { int inserted = 0; int unchanged = 0; };
struct Source {
    QString personId;
    QUrl feedUrl;
    std::optional<qint64> lastSuccessUtc;
    bool stale = false;
    qint64 refreshGeneration = 0;
};

care_contract::Result<Batch> parse(const QByteArray& json);
care_contract::Result<Report> registerBatch(QSqlDatabase& db, const Batch& batch);
care_contract::Result<Report> registerFile(QSqlDatabase& db, const QString& path);
care_contract::Result<care_contract::Page<Source>> listSources(
    QSqlDatabase& db, const care_contract::PageRequest& page);
care_contract::Result<care_contract::Page<Source>> listSourcesForPerson(
    QSqlDatabase& db, const QString& personId,
    const care_contract::PageRequest& page);
}
```

`listSources` 和 `listSourcesForPerson` 是供服务端 provider/Qt 读取配置的内部接口，不直接暴露给网络客户端。provider 随后把 `Source.feedUrl` 作为 `beginUpdatesRefresh`、`failUpdatesRefresh` 和 `mergeUpdates` 的 `feedUrl`；不能使用文章地址代替。
