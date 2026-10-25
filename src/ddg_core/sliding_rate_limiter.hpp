// SPDX-License-Identifier: AGPL-3.0-only
//
// Sliding-window limiter (historical default): at most N requests per 60s.

#pragma once

#include "rate_limiter.hpp"

#include <chrono>
#include <list>
#include <mutex>

namespace ddg {

class SlidingRateLimiter : public RateLimiterBase {
public:
    explicit SlidingRateLimiter(int requests_per_minute);

    void acquire() override;
    bool idle() override;

private:
    using Clock = std::chrono::steady_clock;
    using Time  = Clock::time_point;

    void purge_locked(Time now);

    int             rpm_;
    std::mutex      mu_;
    std::list<Time> requests_;
};

}  // namespace ddg
