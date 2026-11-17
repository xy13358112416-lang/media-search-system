#pragma once
#include "care_contract.h"
#include <functional>
class QWidget;
class OnlineCareClient;

QWidget* createOnlineWindow(OnlineCareClient& client, const warehouse_contract::UserInfo& user,
                          std::function<void()> logout, QWidget* parent = nullptr);

// Interface seam for deterministic UI tests; production uses one OnlineCareClient.
QWidget* createOnlineWindow(warehouse_contract::WarehouseService& warehouses,
                          care_contract::CareService& care,
                          const warehouse_contract::UserInfo& user,
                          std::function<void()> logout, QWidget* parent = nullptr);
