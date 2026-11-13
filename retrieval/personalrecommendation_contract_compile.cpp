#include "personalrecommendation_contract.h"
#include "mediarecommendation.h"

#include <type_traits>

namespace {

using namespace recommendation_contract;

static_assert(std::is_same_v<decltype(RecommendationRequest{}.userId), qint64>,
              "Recommendation and account contracts must use the same user ID type");
static_assert(std::is_abstract_v<PersonalRecommendationService>,
              "The contract declares a service; it does not supply a placeholder backend");

using LegacyRecommendation = QStringList (*)(const QList<MediaRecord>&,
                                            const RecommendationOptions&);
static_assert(std::is_same_v<decltype(&recommendMedia), LegacyRecommendation>,
              "The existing recommendation entry point must remain source-compatible");

// Compile a representative Qt caller against the public contract. This code is
// intentionally not executed: no account, history store or new algorithm exists yet.
[[maybe_unused]] QString submitRecommendation(PersonalRecommendationService& service,
                                             qint64 currentUserId, bool explicitType) {
    RecommendationRequest request;
    request.userId = currentUserId;
    request.category = {PreferenceMode::Auto, {}};
    request.fileType = {PreferenceMode::Any, {}};
    request.limit = 5;
    if (explicitType)
        request.fileType = {PreferenceMode::Explicit, QStringLiteral("MP4")};

    const RecommendationResult result = service.recommend(request);
    if (!result.success)
        return result.error;
    QStringList displayedRows;
    for (const QString& mediaId : result.mediaIds)
        displayedRows.append(mediaId);
    return result.reason + displayedRows.join(QLatin1Char('\n'));
}

} // namespace
