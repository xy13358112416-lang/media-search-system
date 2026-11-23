// >>> DEFENSE-NOTE | 头文件导读｜可信人物导入接口
// Batch保留synthetic、provenance及证据URL；parse只解析，importBatch以保存点保证全批次一致。
// 读法：struct说明传哪些数据；class/函数声明说明谁可调用；实际算法去同名.cpp或引用的实现文件。
// <<< DEFENSE-NOTE | 本段讲解结束
#pragma once

#include "care_contract.h"
#include <QByteArray>
#include <QSqlDatabase>
#include <QStringList>

namespace person_credit_import {
// >>> DEFENSE-NOTE | 类型说明｜Person
// 导入人物：稳定ID、姓名、可信身份来源及别名，不按姓名推导ID。
// <<< DEFENSE-NOTE | 本段讲解结束
struct Person {
    QString personId;
    QString name;
    QUrl identitySource;
    QStringList aliases;
};
// >>> DEFENSE-NOTE | 类型说明｜Credit
// 已确认作品—人物—角色关联，同时保留精确资源来源和关联证据URL。
// <<< DEFENSE-NOTE | 本段讲解结束
struct Credit {
    care_contract::ResourceRef resource;
    QString personId;
    care_contract::PersonRole role = care_contract::PersonRole::Actor;
    QUrl creditSource;
};
// >>> DEFENSE-NOTE | 类型说明｜Batch
// 整批导入数据与来源说明，合成夹具显式标记，失败回滚整批写入。
// <<< DEFENSE-NOTE | 本段讲解结束
struct Batch {
    bool synthetic = false;
    QString provenance;
    QList<Person> people;
    QList<Credit> credits;
};
// >>> DEFENSE-NOTE | 类型说明｜Options
// 导入行为选项；allowSynthetic必须显式许可测试夹具，防演示数据混入正式目录。
// <<< DEFENSE-NOTE | 本段讲解结束
struct Options { bool allowSynthetic = false; };
// >>> DEFENSE-NOTE | 类型说明｜Report
// 导入实际新增数量，用于幂等重跑核对，不把输入条数误当新增条数。
// <<< DEFENSE-NOTE | 本段讲解结束
struct Report { int peopleInserted = 0; int aliasesInserted = 0; int creditsInserted = 0; };

care_contract::Result<Batch> parse(const QByteArray& json);
care_contract::Result<Report> importBatch(QSqlDatabase& db, const Batch& batch,
                                          const Options& options = {});
} // namespace person_credit_import
