// SPDX-License-Identifier: AGPL-3.0-only
//
// server.py WebContentFetcher.

#pragma once

#include "constants.hpp"
#include "host_rate_limiter.hpp"
#include "http_transport.hpp"
#include "link_registry.hpp"
#include "log.hpp"
#include "rate_limiter.hpp"
#include "ttl_cache.hpp"

#include <QString>

#include <memory>

namespace ddg {

class WebContentFetcher {
public:
    struct Config {
        bool      allow_private_urls       = false;
        Transport transport;
        int       requests_per_minute      = 20;
        int       host_requests_per_minute = 0;  // 0 disables the per-host limiter
        QString   rate_limit_strategy      = "sliding";
        double    cache_ttl                = 300.0;
        int       cache_max_entries        = 64;
        QString   parse_mode               = "markdown";
    };

    WebContentFetcher(Config config, std::shared_ptr<LinkRegistry> links);
    ~WebContentFetcher() = default;

    QString fetch_and_parse(const QString& url,
                            const LogFn&   log,
                            int            start_index = 0,
                            int            max_length  = 8000,
                            const QString& parse_mode  = QString());

    [[nodiscard]] const Config& config() const { return m_config; }

private:
    void    guard_url(const QString& url) const;
    QString fetch(const QString& url);

    Config                           m_config;
    std::shared_ptr<LinkRegistry>    m_links;
    std::unique_ptr<RateLimiterBase> m_rate_limiter;
    std::unique_ptr<HostRateLimiter> m_host_limiter;  // null when disabled
    TTLCache                         m_cache;
};

}  // namespace ddg
