# 20 用户离线模拟与算法对照

生成器只读取公开样例目录，不创建可登录账号、不读真实历史、不调用模型、不访问数据库。默认 seed=20261115，配合仓库当前 500 条目录，生成 20 用户、90 个完整北京时间自然日（2026-08-17 至 2026-11-15，不含终点）的 6420 条模拟行为：5033 次浏览、1387 次搜索。更换 seed 或目录后数量可变，manifest 记录实际数量和文件 SHA-256。

```powershell
# 仓库根目录；build 已被仓库忽略。输出目录须不存在或为空。
python sample-data/simulation/generate.py --output build/simulation

# 已配置 Qt/CMake 环境，项目以 BUILD_TESTING=ON 构建后：
cmake --build build --target care_simulation_demo
.\build\network\care_simulation_demo.exe build/simulation/algorithm_fixture.json build/simulation/report.json

ctest --test-dir build -R "care_(simulation|footprint|trending|temporal|person)" --output-on-failure
```

报告路径必须是新文件。工具直接写 UTF-8，避免旧版 PowerShell 管道重新编码中文。也可省略报告路径，将 UTF-8 JSON 写标准输出供其他程序读取。

## 文件与用户

- `catalog.csv`：输入目录的原始副本；`users.csv`：独立模拟 user_ref 和场景描述。
- `events.csv`：稳定 event_id、user_ref、动作、媒体 ID/查询、UTC ISO 时间。`manifest.json` 中 synthetic=true，导入器必须为整份数据保留模拟标记。
- `summary.csv`：每人实际次数，供人工核对。
- `algorithm_fixture.json`：离线算法工具专用快照，含 decimal-string 秒时间和虚构来源位置。不是正常客户端网络 payload。
- `report.json`：C++ 实际运行足迹、热榜、动态推荐和消融后的报告，不由 Python 伪造算法结果。

01—05 偏电影，06—10 偏电视剧，11—14 偏音乐；15/16/17 最后 14 天分别转向音乐/电视剧/电影；18 低活跃混合；19 只有最后三天行为；20 无历史。`scenario/earlier_preference/recent_preference` 仅为测试标签，算法不读。综合测试会删除这些字段后再运行实际 C++ 算法，核对原始事件次数与报告。

`people=[]` 是有意的：原目录仅有姓名，不能据此建立可信人物身份。人物模块另有同名、多角色、权限、来源缓存的 C++ 单元测试；服务端人物目录与关联由②实现。

## 对照能说明什么

报告分别展示累计兴趣近似（长期半衰期 1e12 天、长期权重 1，近期/热度/新鲜度为零）、近期兴趣单项、R00-v1 默认混合评分，以及两周前的快照。两周前计算会丢弃未来事件，且不会使用今天的热度。历史发行日多数缺失，工具保留未知，不能靠填日期制造新鲜度收益。

这份报告的公共热度明确限定为最近 7 天**已公布前 50 项**；`heat_scope` 已标记，用于离线演示。生产服务应查询完整窗口有效计数，按 `include/CARE_ALGORITHMS.md` 注入，不受 UI 前 50 项截断影响。

固定 seed 下，15/16 首推转向近期类别，17 的近期单项也转向电影，但默认混合仍以电视剧为首推：长期兴趣 0.774895、近期兴趣 0.270025、热度 1，对比电影的 0.188475、0.704233、1，总分约 0.529223 对 0.497660。保留这个案例解释参数取舍，不强制改成预期答案。模拟数据没有真实曝光或留出相关性标注，不提供虚构的点击率/满意度提升。

## 交给②的导入要求

先在指定演示数据库建立映射：user_ref → 网络用户；media_id/work_id → 已确认的作品及仓库条目。fixture 中 warehouse=1、item=行号只在离线工具成立，不可照搬到实际库。人物标识须独立确认。演示用户的公共参与由独立导入工具明确设置；真实客户端无权指定时间、身份、synthetic 或 public_eligible。

`(映射用户,event_id)` 幂等导入；重复两次事件数不增长。20 号应无历史；所有导入事件均标记 synthetic；不写入真实用户数据库，不使用真实密码。**本目录尚未实现 SQLite 导入器、RPC 或 Qt 页面。**
