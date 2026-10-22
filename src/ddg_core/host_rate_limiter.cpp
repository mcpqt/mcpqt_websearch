// SPDX-License-Identifier: AGPL-3.0-only
// SPDX-License-Identifier: Apache-2.0

#include "host_rate_limiter.hpp"

#include "rate_limiter_factory.hpp"
#include "utils.hpp"

namespace ddg {

HostRateLimiter::HostRateLimiter(std::string strategy, int requests_per_minute)
    : strategy_(std::move(strategy)), rpm_(requests_per_minute) {}

void HostRateLimiter::acquire(const std::string& url) {
    std::string host = url_hostname(url);
    if (host.empty()) host = "unknown";

    RateLimiterBase* limiter = nullptr;
    {
        std::lock_guard<std::mutex> lk(mu_);
        // Drop limiters for hosts that have gone quiet so the map does not grow
        // one entry per distinct host for the life of the server.
        for (auto it = limiters_.begin(); it != limiters_.end();) {
            if (it->first != host && it->second->idle()) {
                it = limiters_.erase(it);
            } else {
                ++it;
            }
        }
        auto it = limiters_.find(host);
        if (it == limiters_.end()) {
            auto owned = make_rate_limiter(strategy_, rpm_);
            it = limiters_.emplace(host, std::move(owned)).first;
        }
        limiter = it->second.get();
    }
    limiter->acquire();
}

}  // namespace ddg
