#pragma once

#include <QStringList>
#include <QtGlobal>

namespace daily_pick_detail {

// splitmix64：确定性 64 位伪随机数，供每日洗牌使用。
inline quint64 splitmix64(quint64& state) {
    quint64 z = (state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

} // namespace daily_pick_detail

// 对 rankedIds 做 Fisher-Yates 洗牌后取前 limit 条。
// 确定性：同 seed 同输入 → 同输出；seed 变化 → 顺序变化。纯函数，无副作用。
inline QStringList dailyPick(const QStringList& rankedIds, quint64 seed, int limit) {
    if (limit <= 0 || rankedIds.isEmpty()) {
        return {};
    }
    QStringList pool = rankedIds;
    quint64 state = seed;
    for (int i = pool.size() - 1; i > 0; --i) {
        const int j = static_cast<int>(daily_pick_detail::splitmix64(state) % static_cast<quint64>(i + 1));
        pool.swapItemsAt(i, j);
    }
    const int count = qMin(limit, pool.size());
    QStringList result;
    result.reserve(count);
    for (int i = 0; i < count; ++i) {
        result.append(pool.at(i));
    }
    return result;
}
