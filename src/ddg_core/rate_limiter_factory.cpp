// SPDX-License-Identifier: AGPL-3.0-only

#include "rate_limiter_factory.hpp"

#include "sliding_rate_limiter.hpp"
#include "token_bucket_limiter.hpp"

#include <stdexcept>

namespace ddg {

std::unique_ptr<RateLimiterBase> make_rate_limiter(const std::string& strategy,
                                                   int                requests_per_minute) {
    if (strategy == "token_bucket") {
        return std::make_unique<TokenBucketLimiter>(requests_per_minute);
    }
    if (strategy == "sliding") {
        return std::make_unique<SlidingRateLimiter>(requests_per_minute);
    }
    throw std::invalid_argument("Unknown rate-limit strategy '" + strategy +
                                "'. Supported: sliding, token_bucket");
}

}  // namespace ddg
