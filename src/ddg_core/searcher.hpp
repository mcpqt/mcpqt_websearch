// SPDX-License-Identifier: AGPL-3.0-only
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

/// Parse a DuckDuckGo "lite" results page into SearchResults.
std::vector<SearchResult> parse_lite_results(const std::string& html, int max_results);

/// Pagination state carried between "lite" result pages. Only the hidden `s`
/// field is required by DuckDuckGo; `vqd` is preserved for compatibility with
/// pages that expose it.
struct LiteNextPage {
    std::string s;
    std::string vqd;
};

/// Extract the next-page form parameters from a "lite" results page.
LiteNextPage extract_lite_next_page(const std::string& html);

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
                                     const std::string& region      = "",
                                     const std::string& timelimit   = "");

    std::string format_results_for_llm(const std::vector<SearchResult>& results) const;

    [[nodiscard]] const Config& config() const { return config_; }

private:
    std::string display_url(const std::string& url) const;
    std::string request(const std::string&                        data_query,
                        const std::string&                        data_region,
                        const std::string&                        s,
                        const std::string&                        vqd,
                        const std::string&                        timelimit,
                        const Context&                            ctx);

    Config                           config_;
    std::shared_ptr<LinkRegistry>    links_;
    std::unique_ptr<RateLimiterBase> rate_limiter_;
    std::unique_ptr<HttpClient>      client_;
};

}  // namespace ddg
