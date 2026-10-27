// SPDX-License-Identifier: AGPL-3.0-only

#include "fetcher.hpp"

#include "blocked_url_error.hpp"
#include "connect_error.hpp"
#include "http_status_error.hpp"
#include "html_extract.hpp"
#include "rate_limiter_factory.hpp"
#include "timeout_error.hpp"
#include "utils.hpp"

#include <QString>

#include <exception>

namespace ddg {
namespace {

constexpr int kMaxRedirects = 5;

const QString kFetchUserAgent =
    QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");

bool is_redirect(int status) {
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

QString content_cache_key(const QString& url, const QString& parse_mode) {
    return normalize_cache_url(url) + QLatin1Char('|') + parse_mode;
}

}  // namespace

WebContentFetcher::WebContentFetcher(Config config, std::shared_ptr<LinkRegistry> links)
    : m_config(std::move(config)),
      m_links(std::move(links)),
      m_rate_limiter(make_rate_limiter(m_config.rate_limit_strategy, m_config.requests_per_minute)),
      m_host_limiter(m_config.host_requests_per_minute > 0
                         ? std::make_unique<HostRateLimiter>(m_config.rate_limit_strategy,
                                                             m_config.host_requests_per_minute)
                         : nullptr),
      m_cache(m_config.cache_ttl, m_config.cache_max_entries) {
    if (!m_config.transport) {
        throw HttpError(QStringLiteral("no HTTP transport configured"));
    }
}

WebContentFetcher::~WebContentFetcher() = default;

void WebContentFetcher::guard_url(const QString& url) const {
    if (!m_config.allow_private_urls) validate_public_url(url);
}

QString WebContentFetcher::fetch(const QString& url) {
    // Follow redirects manually so the SSRF guard runs on every hop.
    QString current = url;
    for (int i = 0; i <= kMaxRedirects; ++i) {
        guard_url(current);
        HttpRequest request;
        request.url        = current;
        request.headers    = {{QStringLiteral("User-Agent"), kFetchUserAgent}};
        request.timeout_ms = 30000;
        HttpResponse resp  = m_config.transport(request);
        if (resp.status == 429) {
            sleep_retry_after(resp.headers);
            resp = m_config.transport(request);
        }
        if (is_redirect(resp.status) && !resp.location.isEmpty()) {
            current = url_join(current, resp.location);
            continue;
        }
        if (resp.status < 200 || resp.status >= 300) {
            throw HttpStatusError(resp.status, resp);
        }
        return resp.body;
    }
    throw HttpError(QStringLiteral("too many redirects (>%1)").arg(kMaxRedirects));
}

QString WebContentFetcher::fetch_and_parse(const QString& url_in,
                                           const LogFn&   log,
                                           int            start_index,
                                           int            max_length,
                                           const QString& parse_mode) {
    QString url = url_in;
    if (is_ref_token(url)) {
        const QString resolved = m_links->resolve(url);
        if (resolved.isNull()) return unknown_ref_error(url);
        url = resolved;
    }

    const QString effective_mode =
        to_lower(parse_mode.isNull() ? m_config.parse_mode : parse_mode);
    if (!contains(kSupportedParseModes, effective_mode)) {
        return QStringLiteral("Error: Unknown parse_mode '%1'. Supported: text, raw, markdown")
            .arg(effective_mode);
    }

    try {
        const QString cache_key =
            m_cache.enabled() ? content_cache_key(url, effective_mode)
                              : QString();
        // A cache hit skips guard_url on purpose: an entry only exists after a
        // guarded fetch of the same normalized URL succeeded.
        QString    text      = cache_key.isNull() ? QString() : m_cache.get(cache_key);
        const bool cache_hit = !text.isNull();

        if (!cache_hit) {
            if (m_host_limiter) m_host_limiter->acquire(url);
            m_rate_limiter->acquire();

            log_info(log, QStringLiteral("Fetching content from: %1 (parse_mode=%2)")
                              .arg(url)
                              .arg(effective_mode));

            const QString html = fetch(url);
            text               = effective_mode == QStringLiteral("raw")
                                     ? html
                                     : html::html_to_text(html, effective_mode, url);
            if (!cache_key.isNull()) m_cache.set(cache_key, text);
        } else {
            log_info(log, QStringLiteral("Cache hit for %1 (parse_mode=%2); "
                                         "skipping download")
                              .arg(url)
                              .arg(effective_mode));
        }

        QString           content = text;
        const qsizetype   total   = content.size();
        const qsizetype   start   = start_index < 0 ? 0 : start_index;
        const qsizetype   len     = max_length < 0 ? 0 : max_length;

        // Apply pagination (text[start : start + max_length]).
        QString    sliced       = start >= total ? QString() : content.mid(start, len);
        const bool is_truncated = start + len < total;

        QString metadata =
            QStringLiteral("\n\n---\n[Content info: Showing characters %1-%2 of %3 total")
                .arg(start)
                .arg(start + sliced.size())
                .arg(total);
        if (is_truncated) {
            metadata += QStringLiteral(". Use start_index=%1 to see more").arg(start + len);
        }
        if (m_cache.enabled()) {
            metadata += QStringLiteral(" | cache=") +
                        (cache_hit ? QStringLiteral("hit") : QStringLiteral("miss"));
        }
        if (effective_mode != QStringLiteral("text")) {
            metadata += QStringLiteral(" | parse=") + effective_mode;
        }
        metadata += QLatin1Char(']');
        sliced += metadata;

        log_info(log, QStringLiteral("Successfully fetched and parsed content (%1 characters)")
                          .arg(sliced.size()));
        return sliced;
    } catch (const BlockedUrlError& e) {
        log_error(log, QStringLiteral("Blocked fetch for %1: %2")
                           .arg(url, QString::fromUtf8(e.what())));
        return QStringLiteral(
                   "Error: Refusing to fetch %1 (%2). This server blocks requests to "
                   "private/internal addresses to prevent SSRF. If this is a trusted "
                   "local deployment, set DDG_ALLOW_PRIVATE_URLS=1 (or pass "
                   "--allow-private-urls).")
            .arg(url, QString::fromUtf8(e.what()));
    } catch (const TimeoutError&) {
        log_error(log, QStringLiteral("Request timed out for URL: %1").arg(url));
        return QStringLiteral("Error: The request timed out while trying to fetch the webpage.");
    } catch (const HttpError& e) {
        log_error(log, QStringLiteral("HTTP error occurred while fetching %1: %2")
                           .arg(url, QString::fromUtf8(e.what())));
        return QStringLiteral("Error: Could not access the webpage (%1)")
            .arg(QString::fromUtf8(e.what()));
    } catch (const std::exception& e) {
        log_error(log, QStringLiteral("Error fetching content from %1: %2")
                           .arg(url, QString::fromUtf8(e.what())));
        return QStringLiteral(
                   "Error: An unexpected error occurred while fetching the webpage (%1)")
            .arg(QString::fromUtf8(e.what()));
    }
}

}  // namespace ddg
