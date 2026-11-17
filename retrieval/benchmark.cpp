#include "mediasearch.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <locale>
#include <random>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
constexpr int repetitions = 31;  // 每个查询、每种策略的计时样本数。
constexpr unsigned seed = 20261110;  // 固定种子，便于重跑同一份测试数据。

struct Strategy {
    SearchStrategy mode;  // 检索执行策略。
    const char* name;      // CSV 中的策略名。
};

constexpr std::array<Strategy, 3> strategies{{
    {SearchStrategy::LinearScan, "linear_scan"},
    {SearchStrategy::FixedIndexOrder, "fixed_index_order"},
    {SearchStrategy::SmallestListFirst, "smallest_list_first"},
}};

struct Workload {
    const char* name;   // CSV 中的场景名。
    SearchQuery query;  // 所有策略共同执行的查询。
};

// 构造热门和稀有条件。每 997 条分别安排一条稀有演员记录和稀有格式记录。
QList<MediaRecord> makeDataset(int size) {
    std::mt19937 random(seed);
    QList<MediaRecord> records;
    records.reserve(size);
    for (int i = 0; i < size; ++i) {
        const bool rareActor = i % 997 == 0;
        const bool rareType = i % 997 == 1;
        const auto directorDraw = random();
        const auto actorDraw = random();
        const auto typeDraw = random();
        MediaRecord record;
        record.id = QStringLiteral("M%1").arg(i + 1, 6, 10, QLatin1Char('0'));
        record.fileName = record.id;
        record.creator = (rareActor || rareType || directorDraw % 100 < 50)
            ? QStringLiteral("Hot Director") : QStringLiteral("Director %1").arg(directorDraw % 127);
        record.actors.append((rareType || actorDraw % 100 < 60)
            ? QStringLiteral("Hot Actor") : QStringLiteral("Actor %1").arg(actorDraw % 251));
        if (rareActor) {
            record.actors.append(QStringLiteral("Rare Actor"));
        }
        if (rareType) {
            record.fileType = QStringLiteral("WEBM");
        } else {
            record.fileType = (rareActor || typeDraw % 100 < 70) ? QStringLiteral("MP4") : QStringLiteral("MKV");
        }
        records.append(record);
    }
    return records;
}

// 时间单位是微秒。比较结果和输出 CSV 都在计时区间外。
bool measure(const MediaSearch& search, int size, const Workload& workload, double buildMs) {
    const QStringList expected = search.search(workload.query, SearchStrategy::LinearScan);
    std::array<std::vector<double>, strategies.size()> samples;
    for (int repeat = -3; repeat < repetitions; ++repeat) {
        // 轮换执行顺序，避免同一种策略总在冷缓存或热缓存时运行。
        for (std::size_t offset = 0; offset < strategies.size(); ++offset) {
            const auto i = (static_cast<std::size_t>(repeat + 3) + offset) % strategies.size();
            const auto start = Clock::now();
            const QStringList ids = search.search(workload.query, strategies[i].mode);
            const auto elapsed = std::chrono::duration<double, std::micro>(Clock::now() - start).count();
            if (ids != expected) {
                std::cerr << "Result mismatch: " << size << ',' << workload.name << ',' << strategies[i].name << '\n';
                return false;
            }
            if (repeat >= 0) {
                samples[i].push_back(elapsed);
            }
        }
    }

    for (std::size_t i = 0; i < strategies.size(); ++i) {
        auto& timings = samples[i];
        std::sort(timings.begin(), timings.end());
        SearchTrace trace;
        search.search(workload.query, strategies[i].mode, &trace);
        std::cout << size << ',' << workload.name << ',' << strategies[i].name << ','
                  << repetitions << ',' << expected.size() << ',' << trace.recordsScanned << ',';
        if (!trace.steps.isEmpty()) {
            std::cout << trace.steps.first().remainingCount;
        }
        const auto p95 = (timings.size() * 95 + 99) / 100 - 1;  // nearest-rank 95 分位。
        std::cout << ',' << trace.steps.size() << ',' << timings[timings.size() / 2]
                  << ',' << timings[p95] << ',' << buildMs << '\n';
    }
    return true;
}

}  // namespace

int main() {
    const std::array<Workload, 4> workloads{{
        {"rare_actor", {"Hot Director", "Rare Actor", "MP4"}},
        {"rare_type", {"Hot Director", "Hot Actor", "WEBM"}},
        {"common_conditions", {"Hot Director", "Hot Actor", "MP4"}},
        {"no_match", {"Hot Director", "Missing Actor", "MP4"}},
    }};
    std::cout.imbue(std::locale::classic());
    std::cout << std::fixed << std::setprecision(3)
              << "record_count,scenario,strategy,repetitions,matches,records_scanned,initial_candidates,"
                 "index_steps,p50_us,p95_us,index_build_ms\n";
    for (const int size : {1000, 10000, 100000}) {
        const QList<MediaRecord> records = makeDataset(size);
        MediaSearch search;
        const auto start = Clock::now();
        search.rebuild(records);
        const double buildMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        for (const Workload& workload : workloads) {
            if (!measure(search, size, workload, buildMs)) {
                return 1;
            }
        }
    }
}
