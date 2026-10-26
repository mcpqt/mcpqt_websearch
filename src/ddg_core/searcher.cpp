// SPDX-License-Identifier: AGPL-3.0-only

#include "searcher.hpp"

#include "connect_error.hpp"
#include "http_transport.hpp"
#include "http_status_error.hpp"
#include "rate_limiter_factory.hpp"
#include "timeout_error.hpp"
#include "utils.hpp"

#include <QByteArray>
#include <QUrl>

#include <map>
#include <sstream>
#include <stdexcept>

namespace ddg {
namespace {

constexpr const char* kBaseUrl = "https://lite.duckduckgo.com/lite";

const std::map<std::string, std::string>& search_headers() {
    static const std::map<std::string, std::string> headers = {
        {"User-Agent",
         "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like "
         "Gecko) Chrome/139.0.0.0 Safari/537.36"},
        {"Accept",
         "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,"
         "image/apng,*/*;q=0.8"},
        {"Accept-Language", "en-US,en;q=0.9"},
        {"Accept-Encoding", "gzip, deflate"},
        {"Connection", "keep-alive"},
        {"Sec-Fetch-Dest", "document"},
        {"Sec-Fetch-Mode", "navigate"},
        {"Sec-Fetch-Site", "same-origin"},
        {"Sec-Fetch-User", "?1"},
        {"Referer", "https://duckduckgo.com/"},
        {"Sec-Ch-Ua", "\"Not;A=Brand\";v=\"99\", \"Google Chrome\";v=\"139\", \"Chromium\";v=\"139\""},
        {"Sec-Ch-Ua-Mobile", "?0"},
        {"Sec-Ch-Ua-Platform", "\"Windows\""},
        {"Upgrade-Insecure-Requests", "1"},
    };
    return headers;
}

const char* lite_safe_search_value(SafeSearchMode m) {
    switch (m) {
        case SafeSearchMode::Strict:   return "1";
        case SafeSearchMode::Moderate: return "";
        case SafeSearchMode::Off:      return "-1";
    }
    return "";
}

/// DuckDuckGo's `df` parameter accepts d/w/m/y. Accept the friendly spellings an
/// AI agent is likely to emit and reject everything else instead of silently
/// searching without a time filter.
std::string normalize_timelimit(const std::string& raw) {
    const std::string v = to_lower(trim(raw));
    if (v.empty() || v == "all" || v == "any" || v == "none") return "";
    if (v == "d" || v == "day" || v == "1d" || v == "past-day") return "d";
    if (v == "w" || v == "week" || v == "1w" || v == "7d" || v == "past-week") return "w";
    if (v == "m" || v == "month" || v == "1m" || v == "30d" || v == "past-month") return "m";
    if (v == "y" || v == "year" || v == "1y" || v == "365d" || v == "past-year") return "y";
    throw std::invalid_argument(
        "Invalid timelimit '" + raw +
        "'; expected day, week, month, year (or d/w/m/y), or empty for no filter");
}

}  // namespace

/// Detect a fingerprint-based block on the HTML search endpoint (server.py
/// _is_search_block). A 202 is a 2xx, so without this the empty page would parse
/// to zero results silently.
bool is_search_block(int status, const std::string& html) {
    if (status == 202 || status == 403) return true;
    if (status == 200 && trim(html).empty()) return true;
    return false;
}

std::string decode_uddg(const std::string& link) {
    const std::string prefix = "//duckduckgo.com/l/?uddg=";
    if (link.compare(0, prefix.size(), prefix) != 0) return link;
    std::string       enc = link.substr(prefix.size());
    const std::size_t amp = enc.find('&');
    if (amp != std::string::npos) enc = enc.substr(0, amp);
    return QUrl::fromPercentEncoding(QByteArray::fromStdString(enc)).toStdString();
}

DuckDuckGoSearcher::DuckDuckGoSearcher(Config config, std::shared_ptr<LinkRegistry> links)
    : config_(std::move(config)),
      links_(std::move(links)),
      rate_limiter_(make_rate_limiter(config_.rate_limit_strategy, config_.requests_per_minute)) {}

DuckDuckGoSearcher::~DuckDuckGoSearcher() = default;

std::string DuckDuckGoSearcher::display_url(const std::string& url) const {
    if (config_.ref_url_threshold == 0 ||
        static_cast<int>(url.size()) <= config_.ref_url_threshold) {
        return url;
    }
    const std::string token = links_->shorten(url);
    return token +
           " (long URL shortened; pass to fetch_content as-is, or call expand_link to "
           "get the full URL)";
}

std::string DuckDuckGoSearcher::request(const std::string& data_query,
                                        const std::string& data_region,
                                        const std::string& s,
                                        const std::string& vqd,
                                        const std::string& timelimit,
                                        const Context&     ctx) {
    std::map<std::string, std::string> data = {
        {"q", data_query},
        {"s", s},
        {"o", "json"},
        {"api", "d.js"},
        {"vqd", vqd},
        {"kl", data_region},
        {"bing_market", data_region},
    };
    if (!timelimit.empty()) data["df"] = timelimit;
    const char* p = lite_safe_search_value(config_.safe_search);
    if (p && *p) data["p"] = p;

    HttpResponse resp =
        http_post_form(config_.transport, kBaseUrl, data, search_headers(), 30000);
    if (resp.status == 429) {
        sleep_retry_after(resp.headers);
        resp = http_post_form(config_.transport, kBaseUrl, data, search_headers(), 30000);
    }
    if (resp.status < 200 || resp.status >= 300) {
        throw HttpStatusError(resp.status, resp);
    }
    if (is_search_block(resp.status, resp.body)) {
        ctx.info("DuckDuckGo returned a block signal (HTTP " +
                 std::to_string(resp.status) +
                 ") to this backend; the installed build only includes the '" +
                 std::string(kCompiledFetchBackend) + "' backend.");
    }
    return resp.body;
}

std::vector<SearchResult> DuckDuckGoSearcher::search(const std::string& query,
                                                     const Context&     ctx,
                                                     int                max_results,
                                                     const std::string& region,
                                                     const std::string& timelimit) {
    try {
        rate_limiter_->acquire();

        const std::string effective_region = region.empty() ? config_.default_region : region;
        std::string       effective_timelimit;
        try {
            effective_timelimit = normalize_timelimit(timelimit);
        } catch (const std::invalid_argument& e) {
            ctx.error(e.what());
            return {};
        }

        ctx.info("Searching DuckDuckGo for: " + query +
                 " (SafeSearch: " + safe_search_name(config_.safe_search) +
                 ", Region: " + (effective_region.empty() ? "default" : effective_region) +
                 ", Time: " + (effective_timelimit.empty() ? "any" : effective_timelimit) +
                 ", backend=" + config_.backend + ")");

        std::vector<SearchResult> results;
        std::string               s   = "0";
        std::string               vqd;

        constexpr int kMaxPages = 5;
        for (int page = 0;
             page < kMaxPages && static_cast<int>(results.size()) < max_results; ++page) {
            std::string html;
            try {
                html = request(query, effective_region, s, vqd, effective_timelimit, ctx);
            } catch (const std::runtime_error& e) {
                ctx.error(e.what());
                break;
            }

            if (html.find("No more results.") != std::string::npos ||
                html.find("No results.") != std::string::npos) {
                break;
            }

            const int  remaining    = max_results - static_cast<int>(results.size());
            auto       page_results = parse_lite_results(html, remaining);
            if (page_results.empty()) break;

            for (auto& result : page_results) {
                result.position = static_cast<int>(results.size()) + 1;
                results.push_back(std::move(result));
            }

            const LiteNextPage next = extract_lite_next_page(html);
            if (next.s.empty() || next.s == s) break;
            s   = next.s;
            vqd = next.vqd;
        }

        ctx.info("Successfully found " + std::to_string(results.size()) + " results");
        return results;
    } catch (const TimeoutError&) {
        ctx.error("Search request timed out");
        return {};
    } catch (const HttpError& e) {
        ctx.error(std::string("HTTP error occurred: ") + e.what());
        return {};
    } catch (const std::exception& e) {
        ctx.error(std::string("Unexpected error during search: ") + e.what());
        return {};
    }
}

std::string DuckDuckGoSearcher::format_results_for_llm(
    const std::vector<SearchResult>& results) const {
    if (results.empty()) {
        std::string message =
            "No results were found for your search query. This could be due to "
            "DuckDuckGo's bot detection or the query returned no matches. Please try "
            "rephrasing your search or try again in a few minutes.";
        message +=
            " If this persists, DuckDuckGo may be blocking this server; try again in "
            "a few minutes.";
        return message;
    }

    std::ostringstream out;
    out << "Found " << results.size() << " search results:\n";
    for (const auto& result : results) {
        out << "\n";
        out << result.position << ". " << result.title << "\n";
        out << "   URL: " << display_url(result.link) << "\n";
        out << "   Summary: " << result.snippet << "\n";
    }
    return out.str();
}

}  // namespace ddg
