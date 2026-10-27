// SPDX-License-Identifier: AGPL-3.0-only

#include "searcher.hpp"

#include "error.hpp"
#include "rate_limiter.hpp"
#include "utils.hpp"

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QUrl>

namespace ddg {
namespace {

const QString kBaseUrl = "https://lite.duckduckgo.com/lite";

const QMap<QString, QString>& search_headers() {
    static const QMap<QString, QString> headers = {
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
        {"Sec-Ch-Ua",
         "\"Not;A=Brand\";v=\"99\", \"Google Chrome\";v=\"139\", \"Chromium\";v=\"139\""},
        {"Sec-Ch-Ua-Mobile", "?0"},
        {"Sec-Ch-Ua-Platform", "\"Windows\""},
        {"Upgrade-Insecure-Requests", "1"},
    };
    return headers;
}

QString lite_safe_search_value(SafeSearchMode m) {
    switch (m) {
        case SafeSearchMode::Strict:   return "1";
        case SafeSearchMode::Moderate: return QString();
        case SafeSearchMode::Off:      return "-1";
    }
    return QString();
}

/// DuckDuckGo's `df` parameter accepts d/w/m/y. Accept the friendly spellings an
/// AI agent is likely to emit and reject everything else instead of silently
/// searching without a time filter.
QString normalize_timelimit(const QString& raw) {
    const QString v = raw.trimmed().toLower();
    if (v.isEmpty() || v == "all" || v == "any" ||
        v == "none")
        return QString();
    if (v == "d" || v == "day" ||
        v == "1d" || v == "past-day")
        return "d";
    if (v == "w" || v == "week" ||
        v == "1w" || v == "7d" ||
        v == "past-week")
        return "w";
    if (v == "m" || v == "month" ||
        v == "1m" || v == "30d" ||
        v == "past-month")
        return "m";
    if (v == "y" || v == "year" ||
        v == "1y" || v == "365d" ||
        v == "past-year")
        return "y";
    throw InvalidArgument(
        QString("Invalid timelimit '%1'; expected day, week, month, year "
                "(or d/w/m/y), or empty for no filter")
            .arg(raw));
}

}  // namespace

/// Detect a fingerprint-based block on the HTML search endpoint (server.py
/// _is_search_block). A 202 is a 2xx, so without this the empty page would parse
/// to zero results silently.
bool is_search_block(int status, const QString& html) {
    if (status == 202 || status == 403) return true;
    if (status == 200 && html.trimmed().isEmpty()) return true;
    return false;
}

QString decode_uddg(const QString& link) {
    const QString prefix = "//duckduckgo.com/l/?uddg=";
    if (!link.startsWith(prefix)) return link;
    QString        enc = link.mid(prefix.size());
    const qsizetype amp = enc.indexOf(QLatin1Char('&'));
    if (amp != -1) enc.truncate(amp);
    return QUrl::fromPercentEncoding(enc.toUtf8());
}

DuckDuckGoSearcher::DuckDuckGoSearcher(Config config, std::shared_ptr<LinkRegistry> links)
    : config_(std::move(config)),
      links_(std::move(links)),
      rate_limiter_(make_rate_limiter(config_.rate_limit_strategy, config_.requests_per_minute)) {
    if (!config_.transport) {
        throw HttpError("no HTTP transport configured");
    }
}

DuckDuckGoSearcher::~DuckDuckGoSearcher() = default;

QString DuckDuckGoSearcher::display_url(const QString& url) const {
    if (config_.ref_url_threshold == 0 ||
        static_cast<int>(url.size()) <= config_.ref_url_threshold) {
        return url;
    }
    const QString token = links_->shorten(url);
    return token +
           " (long URL shortened; pass to fetch_content as-is, or call "
                          "expand_link to get the full URL)";
}

QString DuckDuckGoSearcher::request(const QString& data_query,
                                    const QString& data_region,
                                    const QString& s,
                                    const QString& vqd,
                                    const QString& timelimit,
                                    const LogFn&   log) {
    QMap<QString, QString> data = {
        {"q", data_query},
        {"s", s},
        {"o", "json"},
        {"api", "d.js"},
        {"vqd", vqd},
        {"kl", data_region},
        {"bing_market", data_region},
    };
    if (!timelimit.isEmpty()) data.insert("df", timelimit);
    const QString p = lite_safe_search_value(config_.safe_search);
    if (!p.isEmpty()) data.insert("p", p);

    HttpRequest request;
    request.is_post    = true;
    request.url        = kBaseUrl;
    request.headers    = search_headers();
    request.form       = data;
    request.timeout_ms = 30000;
    HttpResponse resp  = config_.transport(request);
    if (resp.status == 429) {
        sleep_retry_after(resp.headers);
        resp = config_.transport(request);
    }
    if (resp.status < 200 || resp.status >= 300) {
        throw HttpStatusError(resp.status, resp);
    }
    if (is_search_block(resp.status, resp.body)) {
        log_info(log,
                 QString("DuckDuckGo returned a block signal (HTTP %1).")
                     .arg(resp.status));
    }
    return resp.body;
}

std::vector<SearchResult> DuckDuckGoSearcher::search(const QString& query,
                                                     const LogFn&   log,
                                                     int            max_results,
                                                     const QString& region,
                                                     const QString& timelimit) {
    try {
        rate_limiter_->acquire();

        const QString effective_region =
            region.isEmpty() ? config_.default_region : region;
        QString effective_timelimit;
        try {
            effective_timelimit = normalize_timelimit(timelimit);
        } catch (const InvalidArgument& e) {
            log_error(log, e.message());
            return {};
        }

        log_info(log,
                 QString("Searching DuckDuckGo for: %1 (SafeSearch: %2, "
                         "Region: %3, Time: %4, backend=%5)")
                     .arg(query)
                     .arg(safe_search_name(config_.safe_search))
                     .arg(effective_region.isEmpty() ? "default"
                                                     : effective_region)
                     .arg(effective_timelimit.isEmpty() ? "any"
                                                       : effective_timelimit)
                     .arg(config_.backend));

        std::vector<SearchResult> results;
        QString                   s = "0";
        QString                   vqd;

        constexpr int kMaxPages = 5;
        for (int page = 0;
             page < kMaxPages && static_cast<int>(results.size()) < max_results; ++page) {
            QString html;
            try {
                html = request(query, effective_region, s, vqd, effective_timelimit, log);
            } catch (const std::exception& e) {
                log_error(log, QString::fromUtf8(e.what()));
                break;
            }

            if (html.contains("No more results.") ||
                html.contains("No results.")) {
                break;
            }

            const int remaining    = max_results - static_cast<int>(results.size());
            auto      page_results = parse_lite_results(html, remaining);
            if (page_results.empty()) break;

            for (auto& result : page_results) {
                result.position = static_cast<int>(results.size()) + 1;
                results.push_back(std::move(result));
            }

            const LiteNextPage next = extract_lite_next_page(html);
            if (next.s.isEmpty() || next.s == s) break;
            s   = next.s;
            vqd = next.vqd;
        }

        log_info(log, QString("Successfully found %1 results").arg(results.size()));
        return results;
    } catch (const TimeoutError&) {
        log_error(log, "Search request timed out");
        return {};
    } catch (const HttpError& e) {
        log_error(log, QString("HTTP error occurred: %1").arg(e.message()));
        return {};
    } catch (const std::exception& e) {
        log_error(log, QString("Unexpected error during search: %1")
                           .arg(QString::fromUtf8(e.what())));
        return {};
    }
}

QString DuckDuckGoSearcher::format_results_for_llm(
    const std::vector<SearchResult>& results) const {
    if (results.empty()) {
        return "No results were found for your search query. This could be due to "
            "DuckDuckGo's bot detection or the query returned no matches. Please try "
            "rephrasing your search or try again in a few minutes. If this persists, "
            "DuckDuckGo may be blocking this server; try again in a few minutes.";
    }

    QString out = QString("Found %1 search results:\n").arg(results.size());
    for (const auto& result : results) {
        out += QLatin1Char('\n');
        out += QString::number(result.position) + ". " + result.title +
               QLatin1Char('\n');
        out += "   URL: " + display_url(result.link) + QLatin1Char('\n');
        out += "   Summary: " + result.snippet + QLatin1Char('\n');
    }
    return out;
}

}  // namespace ddg
