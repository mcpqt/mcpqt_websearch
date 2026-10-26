// SPDX-License-Identifier: AGPL-3.0-only

#include "fetcher.hpp"

#include "blocked_url_error.hpp"
#include "connect_error.hpp"
#include "http_transport.hpp"
#include "http_status_error.hpp"
#include "html_extract.hpp"
#include "rate_limiter_factory.hpp"
#include "timeout_error.hpp"
#include "utils.hpp"

#include <map>
#include <stdexcept>

namespace ddg {
namespace {

constexpr int kMaxRedirects = 5;

const char* const kFetchUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36";

bool is_redirect(int status) {
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

std::string content_cache_key(const std::string& url,
                              const std::string& backend,
                              const std::string& parse_mode) {
    return normalize_cache_url(url) + "|" + backend + "|" + parse_mode;
}

}  // namespace

WebContentFetcher::WebContentFetcher(Config config, std::shared_ptr<LinkRegistry> links)
    : config_(std::move(config)),
      links_(std::move(links)),
      rate_limiter_(make_rate_limiter(config_.rate_limit_strategy, config_.requests_per_minute)),
      host_limiter_(config_.host_requests_per_minute > 0
                        ? std::make_unique<HostRateLimiter>(config_.rate_limit_strategy,
                                                            config_.host_requests_per_minute)
                        : nullptr),
      cache_(config_.cache_ttl, config_.cache_max_entries) {
    if (!config_.transport) {
        throw HttpError("no HTTP transport configured");
    }
}

WebContentFetcher::~WebContentFetcher() = default;

void WebContentFetcher::guard_url(const std::string& url) const {
    if (!config_.allow_private_urls) validate_public_url(url);
}

std::string WebContentFetcher::fetch(const std::string& url) {
    // Follow redirects manually so the SSRF guard runs on every hop.
    std::string current = url;
    for (int i = 0; i <= kMaxRedirects; ++i) {
        guard_url(current);
        HttpRequest request;
        request.url        = current;
        request.headers    = {{"User-Agent", kFetchUserAgent}};
        request.timeout_ms = 30000;
        HttpResponse resp  = config_.transport(request);
        if (resp.status == 429) {
            sleep_retry_after(resp.headers);
            resp = config_.transport(request);
        }
        if (is_redirect(resp.status) && !resp.location.empty()) {
            current = url_join(current, resp.location);
            continue;
        }
        if (resp.status < 200 || resp.status >= 300) {
            throw HttpStatusError(resp.status, resp);
        }
        return resp.body;
    }
    throw HttpError("too many redirects (>" + std::to_string(kMaxRedirects) + ")");
}

std::string WebContentFetcher::fetch_and_parse(const std::string&                url_in,
                                               const Context&                    ctx,
                                               int                               start_index,
                                               int                               max_length,
                                               const std::optional<std::string>& backend,
                                               const std::optional<std::string>& parse_mode) {
    std::string url = url_in;
    if (is_ref_token(url)) {
        const auto resolved = links_->resolve(url);
        if (!resolved) return unknown_ref_error(url);
        url = *resolved;
    }

    const std::string effective_backend = backend ? *backend : config_.backend;
    if (!contains(kSupportedFetchBackends, effective_backend)) {
        return "Error: Unknown fetch backend '" + effective_backend + "'. Supported: " +
               kSupportedFetchBackends.front() + ", " + kSupportedFetchBackends.back();
    }
    const std::string effective_mode = to_lower(parse_mode ? *parse_mode : config_.parse_mode);
    if (!contains(kSupportedParseModes, effective_mode)) {
        return "Error: Unknown parse_mode '" + effective_mode +
               "'. Supported: text, main, markdown";
    }
    if (effective_backend != kCompiledFetchBackend) {
        ctx.info("Requested backend '" + effective_backend + "' but this build only includes '" +
                 std::string(kCompiledFetchBackend) + "'; using the built-in backend.");
    }

    try {
        const std::optional<std::string> cache_key =
            cache_.enabled()
                ? std::optional<std::string>(
                      content_cache_key(url, std::string(kCompiledFetchBackend), effective_mode))
                : std::nullopt;
        // A cache hit skips guard_url on purpose: an entry only exists after a
        // guarded fetch of the same normalized URL succeeded.
        std::optional<std::string> text = cache_key ? cache_.get(*cache_key) : std::nullopt;
        const bool                 cache_hit = text.has_value();

        if (!cache_hit) {
            if (host_limiter_) host_limiter_->acquire(url);
            rate_limiter_->acquire();

            ctx.info("Fetching content from: " + url + " (backend=" + effective_backend +
                     ", parse_mode=" + effective_mode + ")");

            const std::string html = fetch(url);
            text = html::html_to_text(html, effective_mode);
            if (cache_key) cache_.set(*cache_key, *text);
        } else {
            ctx.info("Cache hit for " + url + " (backend=" + effective_backend +
                     ", parse_mode=" + effective_mode + "); skipping download");
        }

        std::string content = *text;
        const std::size_t total = content.size();
        const std::size_t start =
            start_index < 0 ? 0u : static_cast<std::size_t>(start_index);
        const std::size_t len = max_length < 0 ? 0u : static_cast<std::size_t>(max_length);

        // Apply pagination (text[start : start + max_length]).
        std::string sliced = start >= total ? std::string{} : content.substr(start, len);
        const bool  is_truncated = start + len < total;

        std::string metadata = "\n\n---\n[Content info: Showing characters " +
                               std::to_string(start) + "-" +
                               std::to_string(start + sliced.size()) + " of " +
                               std::to_string(total) + " total";
        if (is_truncated) {
            metadata += ". Use start_index=" + std::to_string(start + len) + " to see more";
        }
        if (cache_.enabled()) {
            metadata += std::string(" | cache=") + (cache_hit ? "hit" : "miss");
        }
        if (effective_mode != "text") {
            metadata += " | parse=" + effective_mode;
        }
        metadata += "]";
        sliced += metadata;

        ctx.info("Successfully fetched and parsed content (" +
                 std::to_string(sliced.size()) + " characters)");
        return sliced;
    } catch (const BlockedUrlError& e) {
        ctx.error("Blocked fetch for " + url + ": " + e.what());
        return "Error: Refusing to fetch " + url + " (" + e.what() +
               "). This server blocks requests to private/internal addresses to prevent "
               "SSRF. If this is a trusted local deployment, set DDG_ALLOW_PRIVATE_URLS=1 "
               "(or pass --allow-private-urls).";
    } catch (const TimeoutError&) {
        ctx.error("Request timed out for URL: " + url);
        return "Error: The request timed out while trying to fetch the webpage.";
    } catch (const HttpError& e) {
        ctx.error("HTTP error occurred while fetching " + url + ": " + e.what());
        return std::string("Error: Could not access the webpage (") + e.what() + ")";
    } catch (const std::exception& e) {
        ctx.error("Error fetching content from " + url + ": " + e.what());
        return std::string("Error: An unexpected error occurred while fetching the "
                           "webpage (") +
               e.what() + ")";
    }
}

}  // namespace ddg
