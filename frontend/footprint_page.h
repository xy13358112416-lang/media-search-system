#pragma once

#include "care_page.h"

CarePage* createFootprintPage(care_contract::CareService& service, std::function<bool()> signedIn = {});
