// SPDX-License-Identifier: Apache-2.0
//
// server.py DuckDuckGoSearcher.

#pragma once

#include "constants.hpp"
#include "context.hpp"
#include "link_registry.hpp"
#include "rate_limiter.hpp"
#include "safe_search_mode.hpp"
#include "search_result.hpp"
#include "tls_verify.hpp"

#include <memory>
#include <string>
#include <vector>

namespace ddg {

class HttpClient;

/// Parse a DuckDuckGo HTML results page into SearchResults (no network access).
/// Exposed as a free function so it can be unit-tested with fixtures.
std::vector<SearchResult> parse_search_results(const std::string& html, int max_results);

/// Decode a `//duckduckgo.com/l/?uddg=...` redirect wrapper into the real URL.
/// Non-wrapped links are returned unchanged.
std::string decode_uddg(const std::string& link);

/// True when an HTML-endpoint response is a fingerprint/empty-page block.
bool is_search_block(int status, const std::string& html);

class DuckDuckGoSearcher {
public:
    struct Config {
        SafeSearchMode safe_search          = SafeSearchMode::Moderate;
        std::string    default_region;
        std::string    backend              = kCompiledFetchBackend;
        TlsVerify      ssl_verify{};
        int            requests_per_minute  = 30;
        std::string    rate_limit_strategy  = "sliding";
        int            ref_url_threshold    = kDefaultRefUrlThreshold;
    };

    DuckDuckGoSearcher(Config config, std::shared_ptr<LinkRegistry> links);
    ~DuckDuckGoSearcher();

    std::vector<SearchResult> search(const std::string& query,
                                     const Context&     ctx,
                                     int                max_results = 10,
                                     const std::string& region      = "");

    std::string format_results_for_llm(const std::vector<SearchResult>& results) const;

    [[nodiscard]] const Config& config() const { return config_; }

private:
    std::string display_url(const std::string& url) const;
    std::string request(const std::string&                        data_query,
                        const std::string&                        data_region,
                        const Context&                            ctx);

    Config                           config_;
    std::shared_ptr<LinkRegistry>    links_;
    std::unique_ptr<RateLimiterBase> rate_limiter_;
    std::unique_ptr<HttpClient>      client_;
};

}  // namespace ddg
