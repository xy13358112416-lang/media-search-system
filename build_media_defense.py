from pathlib import Path
import yaml
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent
OUT = ROOT / "media-defense"
PAGES = OUT / "pages"
MEDIA = OUT / "media"
PAGES.mkdir(parents=True, exist_ok=True)
MEDIA.mkdir(parents=True, exist_ok=True)

W, H = 960, 540
BG, NAVY, TEAL, TEXT = "#F7F8F6", "#18324A", "#3F7777", "#29343D"
ORANGE, MUTED, PALE, LINE, WHITE = "#C96F4A", "#718078", "#E7EEEA", "#CFDAD5", "#FCFDFB"
FONT_DIR = Path("C:/Windows/Fonts")
def font_path(*names):
    for name in names:
        p = FONT_DIR / name
        if p.exists(): return str(p)
    raise FileNotFoundError(names)
CODE_FONT = font_path("consola.ttf")
CJK_FONT = font_path("msyh.ttc", "simhei.ttf")

FLOWS = {
    "login": ["启动", "读取服务器地址", "TCP 连接", "账号验证", "共用登录会话"],
    "warehouse": ["填写信息", "MediaRecord", "服务端权限检查", "SQLite 保存", "Qt 展示"],
    "search": ["媒体记录与索引", "规范化查询", "字段匹配／查列表", "有序交集", "稳定 ID 结果"],
    "basic": ["偏好输入", "候选媒体", "规则打分", "确定性排序", "推荐 ID"],
    "behavior": ["主动点击／搜索", "BehaviorEvent", "服务端校验", "参与设置", "保存／不保存"],
    "footprint": ["时间区间", "本人事件", "北京时间分日", "指标聚合", "足迹报告"],
    "trending": ["公开资格", "用户／日期去重", "三用户门槛", "确定性排序", "热榜结果"],
    "person": ["人物 ID 与关联", "按用户关注", "资讯源配置", "刷新与合并", "缓存读取"],
    "temporal": ["历史与可读快照", "长期／近期画像", "候选评分", "多样性排序", "推荐理由"],
    "warehouse_ai": ["服务端授权候选", "词面召回", "DeepSeek 选择", "ID 与权限复核", "展示／规则回退"],
    "trend_ai": ["已聚合榜单", "用户点击摘要", "DeepSeek 生成", "响应前复核", "摘要／规则回退"],
    "person_news": ["核验线索", "Search 候选", "URL 过滤", "Fetch 与内容核验", "缓存合并"],
    "warm": ["用户选择场景", "Scene 枚举", "browseScene 映射", "Theme 样式", "页面呈现"],
}
MODULES = {
    "login": "01  在线登录与会话", "warehouse": "02  媒体记录与仓库管理",
    "search": "03  检索与索引", "basic": "04  基础规则推荐",
    "behavior": "05  行为采集与隐私设置", "footprint": "06  个人媒体足迹",
    "trending": "07  全站热榜", "person": "08  人物关联与关注",
    "temporal": "09  时序感知推荐", "warehouse_ai": "10  DeepSeek · 仓库智能推荐",
    "trend_ai": "11  DeepSeek · 热榜 AI 摘要", "person_news": "12  DeepSeek · 人物资讯 Search & Fetch",
    "warm": "13  温暖模式",
}

# 摘录直接读取当前工作区源码；多个区间只用于一页同时讲解两个相关操作。
CODES = [
    dict(n=5,module="login",active=1,title="服务器地址从哪里来？",file="frontend/online_startup.cpp",ranges=[(230,240)],hi=[237,239],owner="Qt 同学",bullets=["ConnectionSettings 先持有默认地址，再由配置覆盖。","INI 的 server 组提供预设地址，本机 online 组记住上次成功连接。","配置只保存地址和用户名，不保存密码。"]),
    dict(n=6,module="login",active=2,title="客户端怎样建立 TCP 连接？",file="network/warehouse_client.cpp",ranges=[(129,140)],hi=[133,134,135,140],owner="主逻辑同学",bullets=["connectAttempt_ 标识连接代次，旧回调不能覆盖新连接。","QTcpSocket 发起连接，8 秒定时器限制等待时间。","超时会中止 socket，并返回 ErrorCode::Timeout。"]),
    dict(n=7,module="login",active=3,title="服务端怎样验证账号并签发会话？",file="server/warehouse_server.cpp",ranges=[(454,455),(458,464)],hi=[458,461,464],owner="主逻辑同学",bullets=["按规范化用户名查询 network_users，不在客户端判断密码。","crypto_pwhash_str_verify 校验密码哈希，失败只返回认证失败。","成功后写入 userId，并生成随机 token 保存在服务端会话中。"]),
    dict(n=8,module="login",active=3,title="认证状态怎样保存和清理？",file="network/warehouse_client.cpp",ranges=[(268,270),(290,297)],hi=[269,290,294],owner="主逻辑同学",bullets=["登录响应通过字段校验后，token_ 才保存到客户端会话。","登出先取出旧 token，再用它发送 logout 请求。","失败或断线会清理会话，并完成或取消待处理回调。"]),
    dict(n=9,module="login",active=4,title="登录身份怎样传给所有在线页面？",file="frontend/main.cpp",ranges=[(1232,1238),(1249,1253),(1261,1263)],hi=[1236,1238,1249,1263],owner="Qt 同学",bullets=["默认路径只创建一个 OnlineCareClient，并进入服务器登录。","同一个 client 与 user 交给 OnlineWindow，各页面共享会话。","窗口销毁后再断开连接，避免页面回调访问失效对象。"]),

    dict(n=11,module="warehouse",active=1,title="系统怎样表示一条媒体记录？",file="include/mediarecord.h",ranges=[(9,19)],hi=[10,15,16,17],owner="主逻辑同学",bullets=["id、fileName、fileType 分别表达标识、文件名和类型。","actors 与 awards 使用 QStringList，保留多值字段。","fileSize、viewCount 和 releaseDate 使用明确的值类型。"]),
    dict(n=12,module="warehouse",active=2,title="仓库条目怎样被授权并保存？",file="server/warehouse_server.cpp",ranges=[(903,906),(920,927)],hi=[905,920,925],owner="主逻辑同学",bullets=["非所有者只能读取公开仓库，新增与删除不享有这个例外。","MediaRecord 先由 JSON 反序列化并校验，再形成存储快照。","INSERT 使用参数绑定，条目写入当前已授权的 warehouseId。"]),
    dict(n=13,module="warehouse",active=4,title="仓库操作结果怎样呈现在界面？",file="frontend/warehouse_page.cpp",ranges=[(439,450)],hi=[439,440,442],owner="Qt 同学",bullets=["服务端返回 WarehouseDetail 后，页面更新当前条目数组。","每个 WarehouseItem 转成 QListWidgetItem，并附带作品详情提示。","空仓库与加载失败都有明确状态，不用旧列表冒充新结果。"]),

    dict(n=15,module="search",active=0,title="系统保存哪些检索索引？",file="include/mediasearch.h",ranges=[(16,18),(51,58)],hi=[17,52,53,54,55],owner="你",bullets=["records_ 以媒体 ID 找到规范化后的完整记录。","directors_、actors_、fileTypes_ 都指向有序 ID 列表。","SearchStrategy 让线性扫描与两种索引顺序使用同一匹配规则。"]),
    dict(n=16,module="search",active=0,title="索引怎样从媒体记录建立？",file="retrieval/mediasearch.cpp",ranges=[(51,55),(58,60),(62,64),(67,67)],hi=[52,55,58,60,64,67],owner="你",bullets=["重建先在临时 MediaSearch 中完成，失败不会破坏旧索引。","空 ID 与重复 ID 直接拒绝，字段规范化后再写入 posting list。","所有 ID 列表统一排序，最后一次性替换当前索引。"]),
    dict(n=17,module="search",active=1,title="记录和查询怎样使用同一套字段规范？",file="retrieval/mediasearch.cpp",ranges=[(11,24)],hi=[12,13,15,18,23,24],owner="你",bullets=["导演、演员去除首尾空白，文件类型统一转为大写。","空演员与重复演员在建索引前清理，避免无效 posting。","查询也经过同样处理，因此输入差异不会改变匹配语义。"]),
    dict(n=18,module="search",active=2,title="线性扫描怎样完成精确匹配？",file="retrieval/mediasearch.cpp",ranges=[(121,123),(127,136)],hi=[128,129,130,135],owner="你",bullets=["导演用完整字符串比较，演员在 QStringList 中查完整姓名。","任一非空条件不满足就 continue，体现多条件 AND 关系。","扫描结果最后按 ID 排序，与索引路径保持一致输出。"]),
    dict(n=19,module="search",active=3,title="短列表优先怎样减少交集计算？",file="retrieval/mediasearch.cpp",ranges=[(158,160),(162,163),(166,166),(170,170),(172,175)],hi=[160,162,166,170,173],owner="你",bullets=["先按 posting 数量升序排列，最短列表成为初始候选。","后续条件使用 std::set_intersection，与当前候选求有序交集。","候选为空时停止；每个输入条件仍需读取对应索引。"]),
    dict(n=20,module="search",active=0,title="新增和删除怎样维护索引？",file="retrieval/mediasearch.cpp",ranges=[(90,96),(104,109)],hi=[94,95,96,104,106,108,109],owner="你",bullets=["新增沿用同一规范化流程，再同步更新 records_ 与索引。","删除依据原记录字段定位 posting，不依赖客户端重新提交字段。","所有相关列表清理后才删除 records_ 中的记录。"]),

    dict(n=22,module="basic",active=0,title="推荐请求怎样表达偏好？",file="include/mediarecommendation.h",ranges=[(8,12)],hi=[9,10,11],owner="你",bullets=["preferredCategory 与 preferredFileType 留空时表示不限。","limit 控制最多返回条数，小于等于零时返回空列表。","类别精确匹配，文件类型忽略大小写。"]),
    dict(n=23,module="basic",active=2,title="基础推荐怎样计算分数并确定顺序？",file="retrieval/mediarecommendation.cpp",ranges=[(31,31),(34,36),(38,39),(42,47),(49,49)],hi=[35,39,43,46,49],owner="你",bullets=["类别匹配加 2 分，类型匹配加 1 分，最高热度再加 1 分。","其余记录用同一 maxViews 作分母，直接比较浏览量分子。","分数与热度都相同时按 ID 升序，保证结果可重复。"]),

    dict(n=25,module="behavior",active=0,title="Qt 怎样区分主动点击和自动选中？",file="frontend/online_window.cpp",ranges=[(146,149),(201,207)],hi=[146,148,203],owner="Qt 同学",bullets=["只连接 itemClicked；自动选中和界面刷新不走这个入口。","条目携带稳定 key，再定位到真实仓库与条目来源。","openWork 经 recordMediaView 记录浏览，并展示服务端返回详情。"]),
    dict(n=26,module="behavior",active=1,title="客户端怎样表达浏览或搜索？",file="include/behavior_contract.h",ranges=[(23,30)],hi=[25,26,27,28,29],owner="主逻辑同学",bullets=["eventId 在同一事件重试时保持不变，支持服务端幂等处理。","View 携带 resource，Search 携带 query，两者语义明确区分。","DTO 不含 userId、时间和 synthetic，客户端不能伪造这些字段。"]),
    dict(n=27,module="behavior",active=4,title="服务端怎样避免重复行为重复计数？",file="server/warehouse_server.cpp",ranges=[(793,805)],hi=[795,796,797,799,803,804],owner="主逻辑同学",bullets=["先按当前会话 userId 与 eventId 查询已经保存的事件。","相同动作与内容的重试直接成功，不再次写入。","同一事件编号若对应不同内容，则明确拒绝请求。"]),
    dict(n=28,module="behavior",active=3,title="用户怎样分别控制历史、推荐和公共贡献？",file="include/behavior_contract.h",ranges=[(42,46)],hi=[43,44,45],owner="主逻辑同学",bullets=["historyEnabled 控制是否保存个人历史。","personalizationEnabled 控制推荐是否使用这些历史。","contributePublicTrends 默认关闭，公共热榜需要单独主动授权。"]),

    dict(n=30,module="footprint",active=2,title="统计区间怎样划分北京时间日期？",file="retrieval/care_footprint.cpp",ranges=[(16,22),(25,29),(102,102)],hi=[17,21,22,26,102],owner="你",bullets=["时间范围是 UTC Unix 秒，endUtc 为开区间，所以末日取 end-1。","QTimeZone(Asia/Shanghai) 将事件映射到北京时间自然日。","MaxReportDays 限制日期桶规模，拒绝无效或过长区间。"]),
    dict(n=31,module="footprint",active=3,title="事件怎样聚合成足迹指标？",file="retrieval/care_footprint.cpp",ranges=[(49,51),(63,66),(74,79),(83,83)],hi=[49,51,66,77,78,79,83],owner="你",bullets=["uniqueWorks 用 QSet 去重，事件按时间落入对应日期桶。","类别按标准化字段累加，浏览量与搜索量分别统计。","(personId, role) 在单次作品浏览中去重，再累加人物计数。"]),
    dict(n=32,module="footprint",active=4,title="足迹报告包含哪些结果与覆盖信息？",file="include/care_contract.h",ranges=[(63,72),(78,82)],hi=[65,66,67,68,69,70,71,72,79,80],owner="你 / Qt 同学",bullets=["PeriodFootprint 保存浏览、搜索、独立作品和活跃天数。","categories、people、days 分别承载类别、人物和逐日统计。","coverageStartUtc 表达真实历史起点；comparison 必须显式给出。"]),

    dict(n=34,module="trending",active=0,title="哪些事件有资格进入公开热榜？",file="server/warehouse_server.cpp",ranges=[(822,826),(828,828),(839,842),(844,845)],hi=[825,826,828,839,844,845],owner="主逻辑同学",bullets=["公开资格同时取决于用户主动授权与作品来源是否公开。","关闭个人历史时不保存该事件，也不会留下公共贡献。","publicEligible 在写入行为时由服务端计算并随事件保存。"]),
    dict(n=35,module="trending",active=0,title="公开热榜快照有哪些可信输入？",file="include/care_algorithm_types.h",ranges=[(9,14),(27,32)],hi=[10,13,27,30,31],owner="主逻辑同学",bullets=["userKey 来自服务端身份，publicEligible 来自服务端资格判断。","ObservedBehavior 同时保存事件与当时作品元数据快照。","PublicSnapshot 只接收当前公开作品和可用于公共统计的事件。"]),
    dict(n=36,module="trending",active=1,title="同一用户同一天的重复点击怎样处理？",file="retrieval/care_trending.cpp",ranges=[(10,13),(15,23),(26,26)],hi=[11,17,18,19,20,21,22],owner="你",bullets=["daysByUser 为每个用户保存参与过的北京时间日期集合。","同一用户同一日期第一次出现才增加 effectiveEvents。","重复点击仍保留来源标记，但不会扩大有效行为计数。"]),
    dict(n=37,module="trending",active=2,title="热榜怎样应用公开门槛并排序？",file="retrieval/care_trending.cpp",ranges=[(70,77),(84,90)],hi=[72,76,77,85,87,89],owner="你",bullets=["不足三位不同用户的条目直接跳过，不进入公开结果。","排名先比较 distinctUsers，再比较 effectiveEvents。","两项都相同时使用 stableKey，避免哈希顺序影响输出。"]),

    dict(n=39,module="person",active=0,title="数据库怎样表示人物与作品关联？",file="server/person_schema.cpp",ranges=[(29,32)],hi=[29,31,32],owner="主逻辑同学",bullets=["person_catalog.person_id 是稳定人物主键，姓名只是展示字段。","person_credits 以 (person_id, work_id, role) 作为组合主键。","role 受 CHECK 限制，并为 work_id、person_id 查询建立索引。"]),
    dict(n=40,module="person",active=1,title="关注关系怎样按用户隔离？",file="server/person_store_sqlite.cpp",ranges=[(181,186),(203,207)],hi=[182,186,203,204],owner="主逻辑同学",bullets=["setFollow 接收的是服务端会话解析出的 userId。","取消关注同时限定 user_id 与 person_id，不能影响其他用户。","组合键冲突时 DO NOTHING，并在事务中保存名称和角色快照。"]),
    dict(n=41,module="person",active=2,title="资讯源和文章链接怎样区分？",file="server/person_schema.cpp",ranges=[(34,35)],hi=[34,35],owner="主逻辑同学",bullets=["person_sources.source_url 标识 feed 或检索来源。","person_updates.source_url 保存具体文章链接，两者不混用。","refresh_generation 与 stale 分别记录来源刷新代次和过期状态。"]),
    dict(n=42,module="person",active=3,title="资讯刷新怎样避免旧结果覆盖新缓存？",file="server/person_store_sqlite.cpp",ranges=[(290,294),(428,434)],hi=[290,292,294,428,433,434],owner="主逻辑同学",bullets=["beginUpdatesRefresh 将代次加一，并把当前来源标记为 stale。","提交成功状态时，WHERE 条件限定本次 refresh_generation。","影响行数不等于 1 时进入失败处理，旧资讯不会被新结果误删。"]),

    dict(n=44,module="temporal",active=0,title="推荐算法从哪些数据构造个人快照？",file="include/care_algorithm_types.h",ranges=[(15,26)],hi=[16,18,19,20,21,22,23,24],owner="主逻辑同学",bullets=["events 是当前用户历史，readableWorks 是当前有权访问的候选。","followedPeople 与 notInterestedWorkIds 提供主动关注和排除信号。","publicHeat 是可信公共统计，并携带来源标记。"]),
    dict(n=45,module="temporal",active=1,title="长期与近期兴趣怎样计算？",file="retrieval/care_temporal.cpp",ranges=[(13,15),(57,63),(144,146)],hi=[14,15,57,62,63,144,145,146],owner="你",bullets=["Profile 用 QMap 累加类别和人物权重，并记录分项总量。","addView 将一次浏览的权重累加到对应类别与人物。","exp2(-age / halfLifeDays) 让近期行为在近期画像中衰减更慢。"]),
    dict(n=46,module="temporal",active=2,title="候选作品总分由什么组成？",file="retrieval/care_temporal.cpp",ranges=[(184,189),(195,200)],hi=[186,187,189,195,197,199,200],owner="你",bullets=["长期、近期兴趣分别计算，公共热度用 log1p 归一化。","显式类别、类型与主动关注作为独立加分项。","total 汇总各信号与权重；不感兴趣作品已在候选阶段排除。"]),
    dict(n=47,module="temporal",active=3,title="排序怎样保持稳定并避免同类连续堆叠？",file="retrieval/care_temporal.cpp",ranges=[(209,214),(220,224),(227,228)],hi=[209,210,212,213,222,224,227,228],owner="你",bullets=["先按 total 降序，再按 workId、warehouseId、itemId 确定次序。","同类别连续达到两个时，向后寻找其他类别的正分候选。","每个结果都附带 reason，展示各分项及其权重。"]),

    dict(n=49,module="warehouse_ai",active=0,title="模型候选集怎样由服务端授权并复核？",file="server/recommendation_router.cpp",ranges=[(87,88),(97,102),(110,111),(125,127)],hi=[87,98,101,110,111,126],owner="你",bullets=["readableCandidates 使用当前会话 userId，而不是客户端声明的身份。","pending 保存用户、token、目标仓库和本次候选键集合。","模型返回后重新读取权限，并再次过滤不属于候选的 key。"]),
    dict(n=50,module="warehouse_ai",active=1,title="词面召回怎样控制模型候选规模？",file="network/llm_candidate_recall.cpp",ranges=[(57,58),(64,67),(80,82),(86,90)],hi=[58,65,67,80,81,86,88],owner="你",bullets=["CandidateBudget 限制候选上限，空 key、空标题与重复项不进入。","标题、人物和类别的相关性决定优先召回顺序。","剩余名额按类别轮次补充，避免单一类别占满模型输入。"]),
    dict(n=51,module="warehouse_ai",active=3,title="模型响应怎样校验，失败时怎样回退？",file="network/llm_recommendation.cpp",ranges=[(55,58),(62,65),(68,69),(157,160)],hi=[56,57,58,64,69,157,159],owner="你",bullets=["有效空列表表示没有足够相关候选，不等同于接口失败。","返回 ID 必须属于本次 allowed 集合，重复 ID 与空理由被忽略。","网络失败或响应整体无效时才启用规则推荐 fallback。"]),

    dict(n=53,module="trend_ai",active=0,title="哪些榜单字段会交给模型？",file="network/llm_trend_summary.cpp",ranges=[(39,41),(45,48),(50,51),(191,194)],hi=[39,41,45,48,51,193],owner="你",bullets=["aggregateRows 接收已经通过公开门槛的 TrendingItem。","distinctUsers 与 effectiveEvents 直接复制，模型不能重算排名。","只有解析成功的文本才替换 summary，并标记 summaryFromModel。"]),
    dict(n=54,module="trend_ai",active=1,title="用户怎样主动触发 AI 摘要？",file="frontend/trending_page.cpp",ranges=[(48,60),(72,78)],hi=[51,53,60,74,75,77],owner="你",bullets=["刷新、切换时间与榜单类型都进入 Rules 模式。","只有点击“生成 AI 摘要”按钮才请求 Model 模式。","请求期间禁用摘要按钮，规则刷新会清除旧摘要。"]),
    dict(n=55,module="trend_ai",active=3,title="怎样避免旧摘要套到新榜单？",file="server/trending_summary_router.cpp",ranges=[(21,24),(152,155),(158,158),(163,164)],hi=[24,152,158,163,164],owner="你",bullets=["请求前保存公开榜单证据，排除生成时间和摘要文本本身。","模型返回后重新执行 currentTrending，得到当前榜单。","只有公开条目与精确计数不变，才采用模型摘要。"]),

    dict(n=57,module="person_news",active=0,title="Search 请求怎样限定人物与发布者？",file="server/deepseek_person_news.h",ranges=[(9,14),(20,22)],hi=[10,11,12,13,22],owner="你",bullets=["personId 与 displayName 指定数据库中的稳定人物及展示名。","approvedSite 限定已核验发布者，identityClue 用于人物消歧。","API Key 由服务器端传入 refresh，不在 Qt 请求结构中传输。"]),
    dict(n=58,module="person_news",active=1,title="服务器怎样发起受限的 Search 请求？",file="server/deepseek_person_news.cpp",ranges=[(24,26),(85,88),(146,150)],hi=[24,26,85,88,148,149],owner="你",bullets=["Search URL 固定在服务端，数量、超时和响应大小都有上限。","检索词同时包含人物名、身份线索和 site 限定。","Search 只返回文章候选，随后进入 URL 过滤而不是直接缓存。"]),
    dict(n=59,module="person_news",active=2,title="候选 URL 怎样过滤并进入 Fetch？",file="server/deepseek_person_news.cpp",ranges=[(172,172),(174,176),(179,180),(198,198),(205,206)],hi=[172,174,175,176,179,180,198,205,206],owner="你",bullets=["候选必须使用 HTTPS、默认端口，主机精确匹配 approvedSite。","重复 URL 不进入队列，Fetch 数量最多为 FetchLimit。","每次读取一个候选，成功取得原文后再执行内容核验。"]),
    dict(n=60,module="person_news",active=4,title="核验后的文章怎样生成稳定缓存身份？",file="server/deepseek_person_news.cpp",ranges=[(213,216),(224,227),(192,194)],hi=[215,216,225,226,227,193],owner="你",bullets=["标题和正文必须同时包含人物名与核验线索，否则跳过。","updateId 由文章 URL 的 SHA-256 生成；未知日期保持空值。","全部候选处理完成后 mergeUpdates，失败路径保留旧缓存并标过期。"]),

    dict(n=62,module="warm",active=1,title="场景枚举怎样定义并映射到推荐语义？",file="frontend/warm_mode.h",ranges=[(8,8),(17,26)],hi=[8,20,21,22,23,24],owner="Qt 同学",bullets=["Scene 用 enum class 表示 Neutral、Relax、Focus、Explore。","browseScene 显式映射到推荐契约中的场景值。","场景来自用户主动选择，不是系统对情绪的推断。"]),
    dict(n=63,module="warm",active=3,title="温暖模式怎样改变页面呈现？",file="frontend/warm_mode.h",ranges=[(29,34),(35,40)],hi=[31,32,33,35,37,39,40],owner="Qt 同学",bullets=["Theme 按场景返回标题、横幅色、表面色和推荐氛围。","Relax 还指定 Relaxing 氛围，其余场景保留对应视觉状态。","这些值只影响交互呈现，不修改媒体数据、账户或仓库权限。"]),
]

FLOW_PAGES = [
    dict(n=4,module="login",title="用户如何进入在线系统？",notes=[("输入","服务器地址与账号密码；默认读取预设地址。"),("处理","TCP 连接后由服务端验证账号并签发 token。"),("输出","登录后的全部在线页面共用同一个客户端会话。")]),
    dict(n=10,module="warehouse",title="一条媒体记录怎样被保存并展示？",notes=[("输入","界面收集作品的标识、类型、人物与发行信息。"),("处理","MediaRecord 经服务端权限检查后写入 SQLite。"),("输出","最新 WarehouseItem 返回客户端并更新列表。")]),
    dict(n=14,module="search",title="组合检索怎样从条件得到结果？",notes=[("输入","导演、演员、文件类型可以分别留空或组合。"),("处理","字段规范化后走精确扫描或有序倒排列表交集。"),("输出","多条路径都返回稳定 ID 顺序，便于界面复现。")]),
    dict(n=21,module="basic",title="用户设定偏好后怎样得到推荐？",notes=[("输入","类别、文件类型偏好与最多返回条数。"),("处理","候选按类别、类型和浏览热度打分并确定排序。"),("输出","返回有限数量的推荐 ID，同分顺序可重复。")]),
    dict(n=24,module="behavior",title="哪些用户操作会进入行为数据？",notes=[("输入","只有主动点击或提交搜索才生成行为事件。"),("处理","服务端补充身份与时间，并检查来源和幂等性。"),("输出","个人历史与公共贡献分别受参与设置控制。")]),
    dict(n=29,module="footprint",title="行为数据怎样变成个人足迹？",notes=[("输入","当前账号的事件与显式指定的 UTC 时间区间。"),("处理","按北京时间自然日归档，再聚合类别与人物。"),("输出","报告包含总量、独立作品、活跃天数和逐日数据。")]),
    dict(n=33,module="trending",title="怎样避免把重复点击误当成全站热度？",notes=[("输入","仅使用具备公共资格的事件和当前公开作品。"),("处理","同用户同条目每天最多计一次，再应用三用户门槛。"),("输出","按独立用户与有效行为排序，并标记合成来源。")]),
    dict(n=38,module="person",title="作品怎样关联可信人物并支持关注？",notes=[("输入","稳定人物 ID、作品角色关系和当前用户身份。"),("处理","关注关系按用户隔离，资讯刷新通过代次合并。"),("输出","关注列表和资讯均从服务端可信缓存读取。")]),
    dict(n=43,module="temporal",title="推荐怎样结合长期与近期兴趣？",notes=[("输入","当前用户历史、可读作品、关注与公共热度。"),("处理","两种半衰期形成画像，再结合新鲜度和显式偏好。"),("输出","排序控制同类连续数量，并返回可解释理由。")]),
    dict(n=48,module="warehouse_ai",title="DeepSeek 怎样只推荐用户有权访问的作品？",notes=[("输入","服务端先按当前会话生成可读候选与稳定 key。"),("处理","词面召回控制规模，模型只做语义筛选与理由。"),("输出","响应再次校验 ID 与权限；失败可回到规则推荐。")]),
    dict(n=52,module="trend_ai",title="模型会不会改变热榜排名？",notes=[("输入","已经完成门槛筛选和计数的榜单快照。"),("处理","用户主动触发后，模型只生成概括性文字。"),("输出","响应前重新核对榜单；变化时保留当前规则摘要。")]),
    dict(n=56,module="person_news",title="搜索结果怎样变成可信人物资讯缓存？",notes=[("输入","运维核验的人物、发布者网站与身份线索。"),("处理","Search 提供候选，URL 过滤后 Fetch 并核对原文。"),("输出","通过校验的文章按稳定 ID 合并进服务端缓存。")]),
    dict(n=61,module="warm",title="用户怎样主动选择交互场景？",notes=[("输入","用户明确选择默认、放松、专注或探索场景。"),("处理","Scene 映射到推荐语义，Theme 提供页面视觉状态。"),("输出","界面与提示随之变化，数据权限保持不变。")]),
]
assert len(CODES) == 47 and len(FLOW_PAGES) == 13

# 答辩截图只保留直接支持本页讲解的真实源码行，行号保持原文件编号。
KEEP = {
    9:[1232,1235,1236,1237,1238,1249,1253,1262,1263],
    12:[903,905,906,920,925,926,927],
    13:[438,439,440,441,442,448,449,450],
    15:[16,17,18,52,53,54,55,58],
    16:[51,52,55,58,59,60,64,67],
    17:[11,12,13,14,15,17,18,23,24],
    19:[158,160,166,170,173,174,175],
    20:[90,91,94,95,96,104,105,106,108,109],
    23:[31,35,36,39,43,44,46,47,49],
    25:[146,147,148,203,206],
    27:[795,796,797,799,800,803,804],
    30:[16,17,20,21,22,25,26,102],
    31:[49,50,51,63,64,65,66,74,77,78,79,83],
    32:[63,65,66,67,68,69,70,71,72,78,79,80],
    34:[825,826,828,839,844,845],
    35:[9,10,11,13,27,28,30,31],
    36:[10,11,12,15,17,18,19,20,21,22,26],
    37:[71,72,76,77,85,86,87,88,89],
    40:[182,185,186,203,205],
    44:[15,16,18,19,20,21,22,23,24],
    45:[14,15,57,62,144,145,146],
    46:[186,187,189,197,199,200],
    47:[209,212,213,222,224,228],
    49:[87,98,101,110,111,126],
    50:[58,65,81,86,88],
    51:[55,56,57,58,64,69,159],
    53:[40,41,50,51,191,193],
    54:[48,51,52,53,60,74,77],
    55:[21,24,152,158,163,164],
    58:[24,26,85,88,146,148,149],
    59:[174,175,176,179,180,198,205,206],
    60:[215,216,224,225,226,227,193],
    63:[32,33,34,35,36,37,38,39,40],
}
# 极长的单行建表 SQL 仅截去同一行后半部分，末尾用省略号明确标识。
CLIP = {41:{35:250}}

def fill(color): return {"type":"solid","color":color}
def text_el(eid,bounds,text,size=20,color=TEXT,bold=False,align=("left","top"),font="微软雅黑",line=1.2,wrap=True):
    return {"elementId":eid,"elementType":"text","bounds":list(bounds),"content":{"text":text,"fontSize":size,"color":color,"fontFamily":font,"bold":bold,"lineHeight":line,"align":list(align),"wrap":wrap}}
def rect(eid,bounds,color,border=None,radius=False):
    d={"elementId":eid,"elementType":"shape","bounds":list(bounds),"shapeName":"roundRect" if radius else "rect","fill":fill(color)}
    if radius:d["adjustments"]=[8000]
    if border:d["border"]=border
    return d
def line_el(eid,bounds,color=LINE,width=1,arrow=False,style="solid"):
    d={"elementId":eid,"elementType":"line","bounds":list(bounds),"viewBox":[1,1],"points":"0,0 1,0","curve":"sharp","border":{"style":style,"width":width,"color":color}}
    if arrow:d["arrow"]=[None,"arrow"]
    return d
def vline_el(eid,bounds,color=LINE,width=1,arrow=False,style="solid"):
    d=line_el(eid,bounds,color,width,False,style);d["points"]="0,0 0,1"
    if arrow:d["arrow"]=[None,"arrow"]
    return d
def base_page(page_type="content"):return {"pageType":page_type,"background":fill("$bg"),"elements":[]}
def header(page,n,title,module=None):
    e=page["elements"];label=MODULES[module] if module else "课程设计答辩 · 多媒体文件管理及检索系统"
    e.append(text_el("section",(58,20,700,20),label,11.5,TEAL,True,wrap=False))
    e.append(text_el("title",(58,47,838,45),title,31,NAVY,True,line=1.05))
    e.append(text_el("page-no",(872,22,30,18),f"{n:02d}",11,MUTED,False,("right","top"),"Arial",wrap=False))
def mini_flow(page,module,active):
    e=page["elements"];nodes=FLOWS[module];x0,y,nw,nh,gap=58,108,144,30,31
    for i,label in enumerate(nodes):
        x=x0+i*(nw+gap);is_model=any(k in label for k in ["DeepSeek","Search","Fetch"]);is_user=any(k in label for k in ["主动","用户点击","用户选择"])
        accent=ORANGE if is_model or is_user else TEAL;selected=i==active
        e.append(rect(f"mini-{i}",(x,y,nw,nh),accent if selected else PALE,{"style":"solid","width":1,"color":accent if selected else LINE},True))
        e.append(text_el(f"mini-label-{i}",(x+4,y+2,nw-8,nh-4),label,11.6,"#FFFFFF" if selected else NAVY,selected,("center","middle"),line=1.05))
        if i<len(nodes)-1:e.append(line_el(f"mini-arrow-{i}",(x+nw+6,y+15,gap-12,1),LINE,1.2,True))

FONT_CACHE={}
def char_font(ch,size):
    key=(ord(ch)>=0x2E80,size)
    if key not in FONT_CACHE:FONT_CACHE[key]=ImageFont.truetype(CJK_FONT if key[0] else CODE_FONT,size)
    return FONT_CACHE[key]
def text_width(draw,s,size):return sum(draw.textlength(ch,font=char_font(ch,size)) for ch in s)
def draw_mixed(draw,xy,s,size,color):
    x,y=xy
    for ch in s:
        f=char_font(ch,size);draw.text((x,y),ch,font=f,fill=color);x+=draw.textlength(ch,font=f)
def split_code(draw,s,size,max_width):
    if not s:return [""]
    result,current=[],""
    for ch in s:
        if current and text_width(draw,current+ch,size)>max_width:
            cut=max(current.rfind(" "),current.rfind(","),current.rfind(";"),current.rfind("("))
            if cut>=max(8,int(len(current)*0.55)):
                result.append(current[:cut+1]);current="        "+current[cut+1:]
            else:result.append(current);current="        "
        current+=ch
    result.append(current);return result

def render_code(spec):
    lines=(ROOT/spec["file"]).read_text(encoding="utf-8").splitlines();chosen=[]
    if spec["n"] in KEEP:
        previous=None
        for number in KEEP[spec["n"]]:
            if previous is not None and number!=previous+1:chosen.append((None,""))
            chosen.append((number,lines[number-1]));previous=number
    else:
        for start,end in spec["ranges"]:
            if chosen:chosen.append((None,""))
            chosen.extend((i,lines[i-1]) for i in range(start,end+1))
    actual=[s.rstrip() for n,s in chosen if n is not None and s.strip()]
    common=min((len(s)-len(s.lstrip()) for s in actual),default=0)
    prepared=[]
    for n,s in chosen:
        if n is None:prepared.append((None,None));continue
        value=s[common:].rstrip();limit=CLIP.get(spec["n"],{}).get(n)
        if limit and len(value)>limit:
            clipped=value[:limit].rstrip();comma=clipped.rfind(",")
            if comma>=0:clipped=clipped[:comma+1]
            value=clipped+"  …"
        prepared.append((n,value))
    iw,code_size=1072,34
    tmp=Image.new("RGB",(iw,300),WHITE);d=ImageDraw.Draw(tmp);rows=[]
    for n,s in prepared:
        if n is None:rows.append((None,"...",False))
        else:
            chunks=split_code(d,s,code_size,iw-124)
            rows.extend((n if j==0 else None,ch,n in spec["hi"]) for j,ch in enumerate(chunks))
    if len(rows)>18:raise ValueError(f"page {spec['n']}: {len(rows)} rendered code rows")
    row_h=42 if len(rows)<=13 else (38 if len(rows)<=16 else 36)
    ih=62+row_h*len(rows)+22
    img=Image.new("RGB",(iw,ih),WHITE);d=ImageDraw.Draw(img)
    d.rectangle((0,0,iw-1,ih-1),outline="#D2DEDA",width=2);d.rectangle((0,0,iw,61),fill=PALE)
    d.text((24,14),spec["file"],font=ImageFont.truetype(CJK_FONT,26),fill=TEAL)
    y=74
    for n,s,hi in rows:
        if hi:d.rectangle((60,y-4,iw-18,y+row_h-5),fill="#E4EFEA")
        if n is not None:
            nf=ImageFont.truetype(CODE_FONT,23);label=str(n);d.text((48-d.textlength(label,font=nf),y+4),label,font=nf,fill="#849189")
        draw_mixed(d,(82,y),s,code_size,"#91A098" if s=="..." else (TEAL if hi else TEXT));y+=row_h
    path=MEDIA/f"code-{spec['n']:02d}.png";img.save(path,optimize=True)
    return path,img.height/2,len(rows)

def bullet_line_count(text):
    # 229 px 文本框扣除渲染器左右内边距后，按约 210 px 有效宽度估算。
    font=ImageFont.truetype(CJK_FONT,38);d=ImageDraw.Draw(Image.new("RGB",(1,1)))
    count,current=1,""
    for ch in text:
        if current and d.textlength(current+ch,font=font)>420:
            count+=1;current=ch
        else:current+=ch
    return count

def code_page(spec):
    page=base_page();header(page,spec["n"],spec["title"],spec["module"]);mini_flow(page,spec["module"],spec["active"])
    path,h,rows=render_code(spec);y=150 if rows>16 else 161
    page["elements"].append({"elementId":"code","elementType":"image","bounds":[58,y,536,h],"src":f"media/{path.name}","fit":{"mode":"fill"}})
    page["elements"].append(text_el("code-caption",(58,y+h+4,536,18),"真实代码摘录 · ... 表示省略 · 蓝绿色底为关键行",11.5,MUTED))
    x=642;page["elements"].append(text_el("look",(x,163,260,23),"看这一段",13,TEAL,True,wrap=False))
    yy=198
    for i,bullet in enumerate(spec["bullets"]):
        height=bullet_line_count(bullet)*25+2
        if yy-3+height>480:raise ValueError(f"page {spec['n']}: right-side bullets overflow")
        page["elements"].append(text_el(f"bullet-no-{i}",(x,yy,27,24),f"0{i+1}",13,TEAL,True,font="Arial",wrap=False))
        page["elements"].append(text_el(f"bullet-{i}",(x+31,yy-3,229,height),bullet,19,TEXT,False,line=1.3))
        yy+=height+10
    page["elements"].append(text_el("owner",(642,495,260,18),f"讲解：{spec['owner']}",11.5,MUTED,False,("right","top")))
    return page

def flow_page(spec):
    page=base_page();header(page,spec["n"],spec["title"],spec["module"]);e=page["elements"];nodes=FLOWS[spec["module"]]
    x0,y,nw,nh,gap=54,190,152,88,23
    for i,label in enumerate(nodes):
        x=x0+i*(nw+gap);is_model=any(k in label for k in ["DeepSeek","Search","Fetch"]);is_user=any(k in label for k in ["主动","用户点击","用户选择"])
        accent=ORANGE if is_model or is_user else TEAL
        e.append(rect(f"flow-{i}",(x,y,nw,nh),WHITE,{"style":"solid","width":1.6,"color":accent},True))
        e.append(text_el(f"flow-num-{i}",(x+14,y+11,30,16),f"0{i+1}",11,accent,True,font="Arial",wrap=False))
        e.append(text_el(f"flow-label-{i}",(x+12,y+34,nw-24,43),label,17,NAVY,True,("center","middle"),line=1.12))
        if i<4:e.append(line_el(f"flow-arrow-{i}",(x+nw+5,y+44,gap-10,1),TEAL,1.4,True))
    e.append(text_el("flow-caption",(58,299,844,24),"数据沿箭头传递；橙色表示用户主动操作或模型调用。",13,MUTED,False,("center","top")))
    for i,(label,body) in enumerate(spec["notes"]):
        yy=353+i*48;e.append(text_el(f"note-label-{i}",(76,yy,55,26),label,15,TEAL,True,wrap=False));e.append(text_el(f"note-body-{i}",(146,yy-2,730,36),body,19,TEXT,line=1.25))
        if i<2:e.append(line_el(f"note-line-{i}",(76,yy+37,784,1),LINE,0.8))
    return page

def cover_page():
    p=base_page("cover");e=p["elements"]
    e.append(text_el("kicker",(66,62,500,26),"课程设计答辩 · 系统设计与真实代码",14,TEAL,True))
    e.append(text_el("title",(66,139,485,118),"懂用户的\n多媒体管理系统",42,NAVY,True,line=1.14))
    e.append(text_el("subtitle",(69,281,430,66),"从作品信息管理出发，逐步连接主动行为、\n个人足迹、全站热榜与推荐服务。",20,TEXT,line=1.5))
    e.append(line_el("cover-rule",(67,378,365,1),TEAL,2));e.append(text_el("cover-meta",(69,397,430,24),"Qt 客户端  /  TCP 服务端  /  SQLite  /  DeepSeek",14,MUTED))
    labels=[("媒体","作品信息与权限"),("行为","主动浏览与搜索"),("服务","足迹 · 热榜 · 推荐")]
    for i,(a,b) in enumerate(labels):
        y=130+i*110;e.append(rect(f"cover-node-{i}",(619,y,272,76),WHITE,{"style":"solid","width":1.4,"color":TEAL},True));e.append(text_el(f"cover-a-{i}",(642,y+12,64,26),a,22,TEAL,True));e.append(text_el(f"cover-b-{i}",(718,y+15,155,28),b,16,TEXT))
        if i<2:e.append(vline_el(f"cover-link-{i}",(754,y+78,1,29),TEAL,1.5,True))
    e.append(text_el("cover-page",(854,480,38,18),"01",11,MUTED,font="Arial",align=("right","top")));return p

def scope_page():
    p=base_page();header(p,2,"我们在原题基础上增加了什么？");e=p["elements"]
    e.append(text_el("left-head",(76,133,320,36),"原题要求",24,NAVY,True));e.append(text_el("right-head",(552,133,320,36),"项目扩展",24,TEAL,True));e.append(line_el("left-line",(76,179,320,1),NAVY,1.8));e.append(line_el("right-line",(552,179,320,1),TEAL,1.8))
    left=["媒体分类与信息管理","增删、排序与组合检索","按明确规则生成推荐"];right=["在线账户与共享会话","主动行为、足迹与公共热榜","人物关注与时序／智能推荐"]
    for col,items,x,c in [(0,left,76,NAVY),(1,right,552,TEAL)]:
        for i,s in enumerate(items):
            y=214+i*65;e.append(text_el(f"num-{col}-{i}",(x,y,30,24),f"0{i+1}",13,c,True,font="Arial"));e.append(text_el(f"item-{col}-{i}",(x+39,y-4,291,42),s,20,TEXT,line=1.3))
    e.append(line_el("bridge",(442,242,65,1),ORANGE,1.8,True));e.append(text_el("bridge-label",(419,264,112,58),"管理作品\n理解兴趣",17,ORANGE,True,("center","top"),line=1.35));e.append(text_el("bottom",(76,445,784,31),"扩展的是服务能力；身份、权限和数据校验仍由系统明确负责。",20,NAVY,True,("center","top")));return p

def architecture_page(final=False):
    p=base_page("final" if final else "content");header(p,64 if final else 3,"从媒体管理到理解用户" if final else "一次用户操作怎样变成系统结果？");e=p["elements"]
    if not final:
        boxes=[(74,169,210,115,"Qt 客户端","发起操作 · 展示结果"),(375,169,220,115,"TCP 服务端","认证 · 权限 · 业务路由"),(687,169,199,115,"SQLite + 算法","持久化 · 检索 · 推荐")]
        for i,(x,y,w,h,a,b) in enumerate(boxes):
            e.append(rect(f"arch-{i}",(x,y,w,h),WHITE,{"style":"solid","width":1.6,"color":TEAL},True));e.append(text_el(f"arch-a-{i}",(x+12,y+22,w-24,31),a,23,NAVY,True,("center","middle")));e.append(text_el(f"arch-b-{i}",(x+12,y+65,w-24,24),b,15,TEXT,False,("center","middle")))
        e.append(line_el("arch-arrow-1",(294,226,70,1),TEAL,2,True));e.append(line_el("arch-arrow-2",(606,226,69,1),TEAL,2,True));e.append(rect("model",(418,329,135,56),BG,{"style":"dash","width":1.3,"color":ORANGE},True));e.append(text_el("model-label",(426,342,119,28),"DeepSeek（可选）",16,ORANGE,True,("center","middle")));e.append(vline_el("model-link",(485,289,1,34),ORANGE,1.4,True,"dash"))
        notes=["Qt 负责交互与展示，不自行决定服务端权限。","服务端验证当前会话，再访问数据库或调用算法。","模型是可选服务；失败与异常路径由服务端处理。"]
        for i,s in enumerate(notes):e.append(text_el(f"arch-note-{i}",(95+i*275,433,250,48),f"0{i+1}  {s}",16,TEXT,line=1.3))
    else:
        labels=[("作品","MediaRecord\n仓库与权限"),("主动行为","浏览 · 搜索\n用户参与设置"),("足迹 / 热榜","个人聚合\n公共去重与门槛"),("推荐服务","规则 · 时序\nDeepSeek 辅助")]
        for i,(a,b) in enumerate(labels):
            x=56+i*223;e.append(rect(f"final-node-{i}",(x,162,186,130),WHITE,{"style":"solid","width":1.5,"color":TEAL},True));e.append(text_el(f"final-a-{i}",(x+12,183,162,30),a,23,NAVY,True,("center","middle")));e.append(text_el(f"final-b-{i}",(x+12,229,162,45),b,16,TEXT,False,("center","middle"),line=1.35))
            if i<3:e.append(line_el(f"final-arrow-{i}",(x+191,227,26,1),TEAL,1.8,True))
        e.append(text_el("final-caption",(78,316,804,29),"模型参与语义筛选与文字表达；候选权限、统计口径和缓存身份仍由系统校验。",18,TEAL,True,("center","top")))
        notes=["原题部分：媒体管理、组合检索和基础规则推荐。","扩展部分：在线会话、行为分析、人物关注与时序推荐。","答辩展示：每个模块都能从数据流定位到真实实现。"]
        for i,s in enumerate(notes):e.append(text_el(f"final-note-{i}",(107,376+i*38,750,28),f"0{i+1}   {s}",18,TEXT))
    return p

def make_all():
    specials={1:cover_page(),2:scope_page(),3:architecture_page(),64:architecture_page(True)}
    for spec in FLOW_PAGES:specials[spec["n"]]=flow_page(spec)
    for spec in CODES:specials[spec["n"]]=code_page(spec)
    refs=[]
    for n in range(1,65):
        rel=f"pages/{n:02d}.page";(OUT/rel).write_text(yaml.safe_dump(specials[n],allow_unicode=True,sort_keys=False,width=1000),encoding="utf-8");refs.append(rel)
    deck={"version":"v2","title":"懂用户的多媒体管理系统 · 课程设计答辩","size":[W,H],"theme":{"colors":{"bg":BG,"primary":NAVY,"secondary":TEAL,"text":TEXT,"accent":ORANGE,"muted":MUTED},"textStyles":{"title":{"fontSize":31,"fontFamily":"微软雅黑","color":"$primary","bold":True},"body":{"fontSize":20,"fontFamily":"微软雅黑","color":"$text","lineHeight":1.35}}},"pages":refs}
    (OUT/"media-defense.pptd").write_text(yaml.safe_dump(deck,allow_unicode=True,sort_keys=False,width=1000),encoding="utf-8")
    print(f"Generated {len(refs)} pages and {len(CODES)} real-code images in {OUT}")
if __name__=="__main__":make_all()
