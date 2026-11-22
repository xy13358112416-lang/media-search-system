// >>> DEFENSE-NOTE | 头文件导读｜所有路径可复用的每日洗牌
// 纯函数：相同输入顺序+相同种子得到相同结果；不访问用户、数据库或网络。换日不保证六条全换。
// 读法：struct说明传哪些数据；class/函数声明说明谁可调用；实际算法去同名.cpp或引用的实现文件。
// <<< DEFENSE-NOTE | 本段讲解结束
#pragma once

#include <QStringList>
#include <QtGlobal>

namespace daily_pick_detail {

// splitmix64：确定性 64 位伪随机数，供每日洗牌使用。
// >>> DEFENSE-NOTE | splitmix64 = 可复现的64位伪随机数发生器
// state引用会被推进；移位、异或和乘法混合比特。这不是密码学随机数，不用于密码/token。
// <<< DEFENSE-NOTE | 本段讲解结束
inline quint64 splitmix64(quint64& state) {
    quint64 z = (state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

} // namespace daily_pick_detail

// 对 rankedIds 做 Fisher-Yates 洗牌后取前 limit 条。
// 确定性：同 seed 同输入 → 同输出；seed 变化 → 顺序变化。纯函数，无副作用。
// >>> DEFENSE-NOTE | dailyPick = 候选洗牌后取前若干条
// 参数rankedIds是原候选顺序，seed固定伪随机序列，limit是上限。
// 复制输入后用Fisher–Yates从后往前交换，再取前min(limit,n)条；一般时间O(n)，复制容器空间O(n)。
// <<< DEFENSE-NOTE | 本段讲解结束
inline QStringList dailyPick(const QStringList& rankedIds, quint64 seed, int limit) {
    if (limit <= 0 || rankedIds.isEmpty()) {
        return {};
    }
    QStringList pool = rankedIds;
    quint64 state = seed;
    // >>> DEFENSE-NOTE | 洗牌步骤｜每轮选0..i的位置交换
    // 取模将伪随机数映射到合法下标；不会增删作品，只改变顺序。
    // 这里是伪随机取模实现，不要宣称密码学安全或数学上完全无取模偏差。
    // <<< DEFENSE-NOTE | 本段讲解结束
    for (int i = pool.size() - 1; i > 0; --i) {
        const int j = static_cast<int>(daily_pick_detail::splitmix64(state) % static_cast<quint64>(i + 1));
        pool.swapItemsAt(i, j);
    }
    // >>> DEFENSE-NOTE | 输出步骤｜不足limit就全部返回
    // limit<=0或空输入提前返回空；相同输入与种子可复现。候选变化时，即使同一天也可以变化。
    // <<< DEFENSE-NOTE | 本段讲解结束
    const int count = qMin(limit, pool.size());
    QStringList result;
    result.reserve(count);
    for (int i = 0; i < count; ++i) {
        result.append(pool.at(i));
    }
    return result;
}
