#pragma once

#include "care_page.h"

CarePage* createTrendingPage(care_contract::CareService& service,
    std::function<void(const care_contract::WarehouseItemRef&)> open = {});
