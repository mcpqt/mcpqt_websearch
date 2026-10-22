// SPDX-License-Identifier: AGPL-3.0-only
// SPDX-License-Identifier: Apache-2.0

#include "searcher.hpp"

#include "connect_error.hpp"
#include "http_client.hpp"
#include "http_status_error.hpp"
#include "html_document.hpp"
#include "html_extract.hpp"
#include "html_query.hpp"
#include "html_text.hpp"
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

constexpr const char* kBaseUrl = "https://html.duckduckgo.com/html";

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
        {"Sec-Fetch-Site", "none"},
        {"Sec-Fetch-User", "?1"},
        {"Sec-Ch-Ua", "\"Not;A=Brand\";v=\"99\", \"Google Chrome\";v=\"139\", \"Chromium\";v=\"139\""},
        {"Sec-Ch-Ua-Mobile", "?0"},
        {"Sec-Ch-Ua-Platform", "\"Windows\""},
        {"Upgrade-Insecure-Requests", "1"},
    };
    return headers;
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

std::vector<SearchResult> parse_search_results(const std::string& html, int max_results) {
    std::vector<SearchResult> results;

    html::Document doc = html::Document::parse(html);
    if (!doc.valid()) return results;
    lxb_dom_node_t* root = doc.root();

    for (lxb_dom_node_t* result : html::select(root, ".result")) {
        lxb_dom_node_t* title_elem = html::select_one(result, ".result__title");
        if (!title_elem) continue;
        lxb_dom_node_t* link_elem = html::find(title_elem, "a");
        if (!link_elem) continue;

        const std::string title = html::get_text(link_elem, "", true);
        std::string       link  = html::attr(link_elem, "href");

        if (link.find("y.js") != std::string::npos) continue;  // skip ad results
        link = decode_uddg(link);

        lxb_dom_node_t*   snippet_elem = html::select_one(result, ".result__snippet");
        const std::string snippet =
            snippet_elem ? html::get_text(snippet_elem, "", true) : "";

        results.push_back(
            SearchResult{title, link, snippet, static_cast<int>(results.size()) + 1});
        if (static_cast<int>(results.size()) >= max_results) break;
    }
    return results;
}

DuckDuckGoSearcher::DuckDuckGoSearcher(Config config, std::shared_ptr<LinkRegistry> links)
    : config_(std::move(config)),
      links_(std::move(links)),
      rate_limiter_(make_rate_limiter(config_.rate_limit_strategy, config_.requests_per_minute)),
      client_(std::make_unique<HttpClient>(config_.ssl_verify)) {}

// Out-of-line so unique_ptr<HttpClient> sees a complete type (http_client.hpp).
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
                                        const Context&     ctx) {
    const std::map<std::string, std::string> data = {
        {"q", data_query},
        {"b", ""},
        {"kl", data_region},
        {"kp", safe_search_value(config_.safe_search)},
    };

    HttpResponse resp = client_->post_form(kBaseUrl, data, search_headers(), 30000);
    if (resp.status == 429) {
        sleep_retry_after(resp.headers);
        resp = client_->post_form(kBaseUrl, data, search_headers(), 30000);
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
                                                     const std::string& region) {
    try {
        rate_limiter_->acquire();

        const std::string effective_region = region.empty() ? config_.default_region : region;

        ctx.info("Searching DuckDuckGo for: " + query +
                 " (SafeSearch: " + safe_search_name(config_.safe_search) +
                 ", Region: " + (effective_region.empty() ? "default" : effective_region) +
                 ", backend=" + config_.backend + ")");

        std::string html;
        try {
            html = request(query, effective_region, ctx);
        } catch (const std::runtime_error& e) {
            ctx.error(e.what());
            return {};
        }

        std::vector<SearchResult> results = parse_search_results(html, max_results);

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
        // The Python server suggests installing the optional curl_cffi backend when
        // it is absent; here the equivalent is building against the curl backend.
        if (std::string(kCompiledFetchBackend) != "curl") {
            message +=
                " If this persists, DuckDuckGo may be blocking this server's TLS "
                "fingerprint; building with -DDDG_HTTP_BACKEND=curl enables using the "
                "system curl backend, which typically resolves it.";
        }
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
