// SPDX-License-Identifier: AGPL-3.0-only
//
// server.py WebContentFetcher.

#pragma once

#include "constants.hpp"
#include "log.hpp"
#include "host_rate_limiter.hpp"
#include "http_transport.hpp"
#include "link_registry.hpp"
#include "rate_limiter.hpp"
#include "ttl_cache.hpp"

#include <memory>
#include <optional>
#include <string>

namespace ddg {

class WebContentFetcher {
public:
    struct Config {
        std::string backend                 = kCompiledFetchBackend;
        bool        allow_private_urls      = false;
        Transport   transport;
        int         requests_per_minute     = 20;
        int         host_requests_per_minute = 0;  // 0 disables the per-host limiter
        std::string rate_limit_strategy     = "sliding";
        double      cache_ttl               = 300.0;
        int         cache_max_entries       = 64;
        std::string parse_mode              = "text";
    };

    WebContentFetcher(Config config, std::shared_ptr<LinkRegistry> links);
    ~WebContentFetcher();

    std::string fetch_and_parse(const std::string&                url,
                                const LogFn&                      log,
                                int                               start_index = 0,
                                int                               max_length  = 8000,
                                const std::optional<std::string>& backend     = std::nullopt,
                                const std::optional<std::string>& parse_mode  = std::nullopt);

    [[nodiscard]] const Config& config() const { return config_; }

private:
    void        guard_url(const std::string& url) const;
    std::string fetch(const std::string& url);

    Config                           config_;
    std::shared_ptr<LinkRegistry>    links_;
    std::unique_ptr<RateLimiterBase> rate_limiter_;
    std::unique_ptr<HostRateLimiter> host_limiter_;  // null when disabled
    TTLCache                         cache_;
};

}  // namespace ddg
