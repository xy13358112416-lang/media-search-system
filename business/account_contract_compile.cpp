#include "account_history_contract.h"
#include <type_traits>

// 验证共享声明可在当前 C++17/Qt 构建环境中被包含。
static_assert(std::is_abstract_v<account_contract::AccountHistoryService>);
static_assert(std::is_same_v<decltype(account_contract::UserIdentity{}.userId), qint64>);
static_assert(std::is_same_v<decltype(account_contract::MediaHistoryEntry{}.mediaId), QString>);
int main() { return 0; }
