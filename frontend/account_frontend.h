#pragma once

#include <QString>

class QApplication;
class MediaLibrary;

namespace account_contract { class AccountHistoryService; }
namespace recommendation_contract { class PersonalRecommendationService; }

// The application supplies a loaded media library and the concrete business services.
int runAccountFrontend(QApplication& app, MediaLibrary& library,
                       account_contract::AccountHistoryService& accounts,
                       recommendation_contract::PersonalRecommendationService& recommendations,
                       const QString& databasePath);
