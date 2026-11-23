// >>> MAIN-DEFENSE | 主逻辑头文件导读
// 人物数据库迁移：核对版本、列、主键/外键/约束；在事务内升级到受支持结构。版本号相同也要查结构，不能假定同名表必然正确。
// 阅读顺序：输入数据结构 → 返回状态/可选数据 → 方法声明 → 私有成员。引用/指针保存依赖，必须确认由谁拥有及何时释放。
// <<< MAIN-DEFENSE
#pragma once

#include <QString>
class QSqlDatabase;

namespace person_schema {
// Caller owns an active SQLite transaction and commits or rolls it back.
// Does not open connections or create a second database.
// >>> MAIN-DEFENSE | 方法声明｜migrate
// 验证SQLite版本和人物表结构，在事务中执行升级；失败回滚，不伪装空库继续运行。
// <<< MAIN-DEFENSE
bool migrate(QSqlDatabase& db, QString& error);
}
