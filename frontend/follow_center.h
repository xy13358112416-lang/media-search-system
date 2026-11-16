#pragma once

#include "care_page.h"

using PersonWorkViewer = std::function<void(const care_contract::WarehouseItemRef&,
                                            care_contract::Callback<care_contract::ResourceSummary>)>;

CarePage* createFollowCenter(care_contract::CareService& service, PersonWorkViewer viewWork = {});
