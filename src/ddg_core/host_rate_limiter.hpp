// SPDX-License-Identifier: Apache-2.0
//
// Per-host limiter so one site cannot spend the whole fetch quota.

#pragma once

#include "rate_limiter.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace ddg {

class HostRateLimiter {
public:
    HostRateLimiter(std::string strategy, int requests_per_minute);

    void acquire(const std::string& url);

private:
    std::string                                                       strategy_;
    int                                                               rpm_;
    std::mutex                                                        mu_;
    std::unordered_map<std::string, std::unique_ptr<RateLimiterBase>> limiters_;
};

}  // namespace ddg
