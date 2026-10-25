// SPDX-License-Identifier: AGPL-3.0-only
//
// Token-bucket limiter: allows a short burst, then smooths to `rpm`.

#pragma once

#include "rate_limiter.hpp"

#include <chrono>
#include <mutex>
#include <optional>

namespace ddg {

class TokenBucketLimiter : public RateLimiterBase {
public:
    explicit TokenBucketLimiter(int                     requests_per_minute,
                                std::optional<double>   burst = std::nullopt);

    void acquire() override;
    bool idle() override;

private:
    using Clock = std::chrono::steady_clock;
    using Time  = Clock::time_point;

    void refill_locked();

    int        rpm_;
    double     rate_;
    double     burst_;
    double     tokens_;
    Time       updated_ = Clock::now();
    std::mutex mu_;
};

}  // namespace ddg
