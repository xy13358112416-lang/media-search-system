# 可信人物—作品关联导入

`media_person_import` 是服务端操作工具，使用服务器现有 SQLite 库，要求已完成版本 3 迁移、启用外键。它不创建账号、作品或来源，也不按 CSV 姓名猜人物身份。操作人员应先停止服务器、备份数据库，再导入人工复核的映射。客户端不持有数据库，也没有此写入 RPC。

```powershell
& .\build\server\media_person_import.exe D:\demo\server.sqlite D:\demo\reviewed-person-mapping.json
```

输出 JSON 的三个数量是本次实际新增的人物、别名、角色关联；再次导入相同内容为零。任何验证、来源匹配、冲突或 SQL 错误都会使整批回滚。已有相同 ID 的姓名/身份来源冲突，或相同人物/作品/角色的证据来源冲突，直接报错，不能静默替换。需要更正已确认资料时应另行审核，不能用导入覆盖。

格式参考 `sample-data/person/import-demo.synthetic.json`。根对象包含：

- `schema` 固定为 `care-person-import-v1`；`synthetic` 必须是布尔值；`provenance` 说明人工核验/来源情况。
- `people` 包含稳定 `person_id`、`name`、HTTPS `identity_source` 和字符串数组 `aliases`。同名可以有不同 ID；相同 ID 代表同一已确认身份。
- `credits` 包含 `person_id`、`role`（`actor` / `director` / `musician`）、HTTPS `credit_source` 和 `resource`。人物可以引用本批人物或数据库里已有的人物。
- `resource.work_id` 及 `source.warehouse_id`、`source.item_id` 必须精确对应服务器已登记且作品仍存在的来源。后两个数字使用十进制字符串，避免跨 JSON 实现丢失整数精度。CSV 行号和影片名称不能替代这三个字段。

HTTPS 地址只记录核验依据，不代表工具自动证明了内容真实。导入不请求网络，不抓网页。真实数据必须由提交者先核对人物与作品身份并留下可复核的证据；找不到证据的作品保持人物未知。

样例是虚构数据，默认拒绝；仅专用测试/演示库可显式允许：

```powershell
& .\build\server\media_person_import.exe --allow-synthetic D:\demo\server.sqlite .\sample-data\person\import-demo.synthetic.json
```

样例里的资源三元组是占位示例，必须替换为该演示库实际登记的值。工具不会为了使样例通过而自动创建来源。`example.invalid` 不是真实资料站点。原先 `confirmed_people.synthetic.json` 含权限边界与拒绝场景，属于算法夹具，不能作为此导入格式使用。`synthetic` 为批次标记，现有目录表不保存逐行合成标记，所以不要把允许合成的批次导入正式库。

程序接口见 `person_credit_import.h`。调用方提供数据库连接，必须在连接所属线程执行；内部使用 SAVEPOINT，因此失败不会回滚调用方早先的其他工作，成功也不会提交调用方外层事务。批次最大 8 MiB JSON、10000 人物及 10000 条关联；每作品已确认人物最多 256 个。

仓储在下次读取资源时即可使用已导入关联；新的主动浏览保存当时人物快照。旧历史保持原样，不能用新目录补造过去的认识。资讯 feed 与本文的身份/作品证据地址是不同用途，导入不修改 `person_sources` 或资讯缓存。

## 给现有演示库添加已核验的人物

停服并备份后，用实际演示库生成映射，再调用导入工具。以下命令在仓库根目录运行。

```powershell
python sample-data/person/prepare_demo_person_mapping.py build/media-demo-200.sqlite build/demo-person-credits.json
.\build\server\media_person_import.exe build/media-demo-200.sqlite build/demo-person-credits.json
```

生成器读取当前库实际分配的作品标识，收录 AFI Catalog 已核验的 Alan Alda 以及三部作品的演员、导演关联。重新启动服务器后可在关注中心搜索 Alan Alda。
