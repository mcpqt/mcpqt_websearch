// SPDX-License-Identifier: Apache-2.0

#include "token_bucket_limiter.hpp"

#include <algorithm>
#include <thread>

namespace ddg {

TokenBucketLimiter::TokenBucketLimiter(int requests_per_minute, std::optional<double> burst)
    : rpm_(requests_per_minute < 1 ? 1 : requests_per_minute),
      rate_(static_cast<double>(rpm_) / 60.0),
      burst_(burst.value_or(static_cast<double>(rpm_))),
      tokens_(burst_) {}

void TokenBucketLimiter::refill_locked() {
    const Time now = Clock::now();
    const double elapsed =
        std::chrono::duration_cast<std::chrono::duration<double>>(now - updated_).count();
    updated_ = now;
    tokens_ = std::min(burst_, tokens_ + elapsed * rate_);
}

bool TokenBucketLimiter::idle() {
    std::lock_guard<std::mutex> lk(mu_);
    refill_locked();
    return tokens_ >= burst_;
}

void TokenBucketLimiter::acquire() {
    double wait_seconds = 0.0;
    {
        std::lock_guard<std::mutex> lk(mu_);
        refill_locked();
        if (tokens_ < 1.0) {
            wait_seconds = rate_ > 0 ? (1.0 - tokens_) / rate_ : 1.0;
        }
        tokens_ -= 1.0;  // Python decrements even when going negative.
    }
    if (wait_seconds > 0) {
        std::this_thread::sleep_for(std::chrono::duration<double>(wait_seconds));
    }
}

}  // namespace ddg
