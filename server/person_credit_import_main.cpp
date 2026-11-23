// >>> MAIN-DEFENSE | 主逻辑文件导读
// 人物可信关联导入CLI：解析文件/数据库选项，调用导入服务并报告结果；不按演员名字自动猜真实人物身份。
// 阅读时先看函数入口说明，再按“处理阶段”往下；到每个return核对成功、空结果和错误是否区别。
// <<< MAIN-DEFENSE
#include "person_credit_import.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTextStream>

// >>> MAIN-DEFENSE | 函数入口｜main
// 本文件的辅助函数。阅读下方实际参数、判断和返回值，服务于上述文件职责；错误分支提前返回，调用者必须检查返回状态。
// 签名提示：int main(int argc, char** argv) 
// 到这里：刚进入该步骤，还没有完成下方I/O或校验；以实际成功返回为准。
// <<< MAIN-DEFENSE
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("media_person_import");
    QCommandLineParser parser;
    parser.setApplicationDescription("Import reviewed person credits into an existing server database (schema 3). Stop the server before operator imports.");
    parser.addHelpOption();
    parser.addOption({"allow-synthetic", "Explicitly allow fictional fixtures in a dedicated demo/test database."});
    parser.addPositionalArgument("database", "Existing server SQLite database.");
    parser.addPositionalArgument("mapping", "Reviewed care-person-import-v1 JSON file.");
    parser.process(app);
    const auto args = parser.positionalArguments();
    // >>> MAIN-DEFENSE | main｜处理阶段 01
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (args.size() != 2) parser.showHelp(2);
    const auto fail = [](const QString& message) { QTextStream(stderr) << message << Qt::endl; return 1; };
    if (!QFileInfo(args[0]).isFile()) return fail("Server database does not exist; initialize it with media_server first.");
    QFile file(args[1]);
    // >>> MAIN-DEFENSE | main｜处理阶段 02
    // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
    // <<< MAIN-DEFENSE
    if (!file.open(QIODevice::ReadOnly) || file.size() > 8 * 1024 * 1024)
        return fail("Cannot read mapping file, or it exceeds 8 MiB.");
    const auto batch = person_credit_import::parse(file.readAll());
    if (!batch.status.ok() || !batch.value) return fail(batch.status.message);
    int code = 0;
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", "person-operator-import");
        db.setDatabaseName(QFileInfo(args[0]).absoluteFilePath());
        db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=5000");
        // >>> MAIN-DEFENSE | main｜处理阶段 03
        // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
        // <<< MAIN-DEFENSE
        if (!db.open()) code = fail("Cannot open server database.");
        else {
            QSqlQuery query(db);
            if (!query.exec("PRAGMA foreign_keys=ON")) code = fail("Cannot enable foreign key checks.");
            else {
                const auto result = person_credit_import::importBatch(db, *batch.value, {parser.isSet("allow-synthetic")});
                // >>> MAIN-DEFENSE | main｜处理阶段 04
                // 检查下一项条件：括号表达式决定是否进入下方处理。带错误return时会立即结束，不执行后续阶段。
                // <<< MAIN-DEFENSE
                if (!result.status.ok() || !result.value) code = fail(result.status.message);
                else {
                    const QJsonObject report{{"synthetic", batch.value->synthetic},
                        {"people_inserted", result.value->peopleInserted},
                        {"aliases_inserted", result.value->aliasesInserted},
                        {"credits_inserted", result.value->creditsInserted}};
                    QTextStream(stdout) << QJsonDocument(report).toJson(QJsonDocument::Compact) << Qt::endl;
                }
            }
        }
        db.close();
    }
    QSqlDatabase::removeDatabase("person-operator-import");
    // >>> MAIN-DEFENSE | main｜处理阶段 05
    // 结束本函数并交付当前结果。检查返回值中的状态及数据：成功空列表与操作失败的含义不同。
    // <<< MAIN-DEFENSE
    return code;
}
