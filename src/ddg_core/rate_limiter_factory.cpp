// SPDX-License-Identifier: AGPL-3.0-only

#include "rate_limiter_factory.hpp"

#include "error.hpp"
#include "sliding_rate_limiter.hpp"
#include "token_bucket_limiter.hpp"

#include <QString>

namespace ddg {

std::unique_ptr<RateLimiterBase> make_rate_limiter(const QString& strategy,
                                                   int            requests_per_minute) {
    if (strategy == QStringLiteral("token_bucket")) {
        return std::make_unique<TokenBucketLimiter>(requests_per_minute);
    }
    if (strategy == QStringLiteral("sliding")) {
        return std::make_unique<SlidingRateLimiter>(requests_per_minute);
    }
    throw InvalidArgument(
        QStringLiteral("Unknown rate-limit strategy '%1'. Supported: sliding, token_bucket")
            .arg(strategy));
}

}  // namespace ddg
