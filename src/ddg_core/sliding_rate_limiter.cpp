// SPDX-License-Identifier: AGPL-3.0-only
// SPDX-License-Identifier: Apache-2.0

#include "sliding_rate_limiter.hpp"

#include <thread>

namespace ddg {

SlidingRateLimiter::SlidingRateLimiter(int requests_per_minute)
    : rpm_(requests_per_minute < 1 ? 1 : requests_per_minute) {}

void SlidingRateLimiter::purge_locked(Time now) {
    while (!requests_.empty() && now - requests_.front() >= std::chrono::minutes(1)) {
        requests_.pop_front();
    }
}

void SlidingRateLimiter::acquire() {
    while (true) {
        double wait_seconds = 0.0;
        {
            std::lock_guard<std::mutex> lk(mu_);
            const Time now = Clock::now();
            purge_locked(now);
            if (static_cast<int>(requests_.size()) < rpm_) {
                requests_.push_back(now);
                return;
            }
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::duration<double>>(
                    now - requests_.front()).count();
            wait_seconds = 60.0 - elapsed;
        }
        if (wait_seconds > 0) {
            std::this_thread::sleep_for(std::chrono::duration<double>(wait_seconds));
        } else {
            // Oldest entry is at/just past the window but still listed (clock
            // resolution pile-up): drop it so we cannot spin on sleep(0).
            std::lock_guard<std::mutex> lk(mu_);
            if (!requests_.empty()) requests_.pop_front();
        }
    }
}

bool SlidingRateLimiter::idle() {
    std::lock_guard<std::mutex> lk(mu_);
    purge_locked(Clock::now());
    return requests_.empty();
}

}  // namespace ddg
