#pragma once

#include "care_page.h"

CarePage* createTemporalPage(care_contract::CareService& service, std::function<care_contract::BrowseScene()> scene);
