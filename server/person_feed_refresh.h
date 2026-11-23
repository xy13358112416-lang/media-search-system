// >>> MAIN-DEFENSE | 主逻辑头文件导读
// RSS/Atom资讯刷新：按已登记的HTTPS源抓取和解析，限制响应与时间，使用begin/merge/fail及generation维护缓存。
// 阅读顺序：输入数据结构 → 返回状态/可选数据 → 方法声明 → 私有成员。引用/指针保存依赖，必须确认由谁拥有及何时释放。
// <<< MAIN-DEFENSE
#pragma once

#include "person_source_config.h"

class QNetworkAccessManager;
namespace person_store { class Store; }

namespace person_feed_refresh {
// Fetches only an already configured source. Store and network must outlive the callback.
void refresh(person_store::Store& store, QNetworkAccessManager& network,
             const person_source_config::Source& source, care_contract::StatusCallback done);
} // namespace person_feed_refresh
