// SPDX-License-Identifier: AGPL-3.0-only
//
// server.py DuckDuckGoSearcher.

#pragma once

#include "constants.hpp"
#include "http_transport.hpp"
#include "link_registry.hpp"
#include "log.hpp"
#include "rate_limiter.hpp"
#include "safe_search_mode.hpp"
#include "search_result.hpp"

#include <QString>

#include <memory>
#include <vector>

namespace ddg {

/// Parse a DuckDuckGo HTML results page into SearchResults (no network access).
/// Exposed as a free function so it can be unit-tested with fixtures.
std::vector<SearchResult> parse_search_results(const QString& html, int max_results);

/// Parse a DuckDuckGo "lite" results page into SearchResults.
std::vector<SearchResult> parse_lite_results(const QString& html, int max_results);

/// Pagination state carried between "lite" result pages. Only the hidden `s`
/// field is required by DuckDuckGo; `vqd` is preserved for compatibility with
/// pages that expose it.
struct LiteNextPage {
    QString s;
    QString vqd;
};

/// Extract the next-page form parameters from a "lite" results page.
LiteNextPage extract_lite_next_page(const QString& html);

/// Decode a `//duckduckgo.com/l/?uddg=...` redirect wrapper into the real URL.
/// Non-wrapped links are returned unchanged.
QString decode_uddg(const QString& link);

/// True when an HTML-endpoint response is a fingerprint/empty-page block.
bool is_search_block(int status, const QString& html);

class DuckDuckGoSearcher {
public:
    struct Config {
        SafeSearchMode safe_search          = SafeSearchMode::Moderate;
        QString        default_region;
        QString        backend              = kCompiledFetchBackend;
        Transport      transport;
        int            requests_per_minute  = 30;
        QString        rate_limit_strategy  = "sliding";
        int            ref_url_threshold    = kDefaultRefUrlThreshold;
    };

    DuckDuckGoSearcher(Config config, std::shared_ptr<LinkRegistry> links);
    ~DuckDuckGoSearcher();

    std::vector<SearchResult> search(const QString& query,
                                     const LogFn&   log,
                                     int            max_results = 10,
                                     const QString& region      = QString(),
                                     const QString& timelimit   = QString());

    QString format_results_for_llm(const std::vector<SearchResult>& results) const;

    [[nodiscard]] const Config& config() const { return config_; }

private:
    QString display_url(const QString& url) const;
    QString request(const QString& data_query,
                    const QString& data_region,
                    const QString& s,
                    const QString& vqd,
                    const QString& timelimit,
                    const LogFn&   log);

    Config                           config_;
    std::shared_ptr<LinkRegistry>    links_;
    std::unique_ptr<RateLimiterBase> rate_limiter_;
};

}  // namespace ddg
