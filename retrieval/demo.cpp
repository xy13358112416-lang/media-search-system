#include "mediasearch.h"

#include <QTextStream>
#include <cstdio>

namespace {

// 创建手写 MediaRecord，演员列表和文件类型用于组合检索。
MediaRecord makeRecord(const QString& id, const QString& name, const QString& director,
                       const QStringList& actors, const QString& fileType) {
    MediaRecord record;
    record.id = id;
    record.fileName = name;
    record.director = director;
    record.actors = actors;
    record.fileType = fileType;
    return record;
}

const char* fieldName(SearchField field) {
    switch (field) {
    case SearchField::Director: return "director";
    case SearchField::Actor: return "actor";
    case SearchField::FileType: return "fileType";
    }
    return "unknown";
}

// 打印本次查询的结果，以及每一步剩下的候选数量。
void printQuery(QTextStream& output, const MediaSearch& search, const SearchQuery& query,
                SearchStrategy strategy, const char* title) {
    SearchTrace trace;
    const QStringList ids = search.search(query, strategy, &trace);
    output << title << ": [" << ids.join(", ") << "] (" << ids.size() << " matches)\n";
    if (strategy == SearchStrategy::LinearScan) {
        output << "  scanned " << trace.recordsScanned << " records\n";
    }
    for (const SearchStep& step : trace.steps) {
        output << "  " << fieldName(step.field) << ": " << step.postingCount
               << " matched alone, " << step.remainingCount << " candidates remain\n";
    }
}

}  // namespace

int main() {
    const QString directorA = QStringLiteral("导演甲");
    const QString directorB = QStringLiteral("导演乙");
    const QString actorA = QStringLiteral("演员甲");
    const QString actorB = QStringLiteral("演员乙");
    const QList<MediaRecord> records{
        makeRecord("M000003", "three.mkv", directorA, {actorA}, "MKV"),
        makeRecord("M000001", "one.mp4", directorA, {actorA, actorB}, "MP4"),
        makeRecord("M000002", "two.mp4", directorA, {actorA}, "MP4"),
        makeRecord("M000004", "four.mp4", directorB, {actorA}, "MP4"),
    };
    MediaSearch search;
    search.rebuild(records);
    const SearchQuery query{directorA, actorB, "MP4"};
    QTextStream output(stdout);
    output << "Query: " << query.director << " + " << query.actor << " + " << query.fileType << '\n';
    printQuery(output, search, query, SearchStrategy::LinearScan, "Linear scan");
    printQuery(output, search, query, SearchStrategy::FixedIndexOrder, "Fixed index order");
    printQuery(output, search, query, SearchStrategy::SmallestListFirst, "Smallest list first");

    search.removeRecord("M000001");
    printQuery(output, search, query, SearchStrategy::SmallestListFirst, "After removing M000001");
}
