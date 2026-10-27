// SPDX-License-Identifier: AGPL-3.0-only
//
// Free-function utilities shared across the port: small string helpers, URL
// helpers, ref:// token helpers, Retry-After parsing, and the SSRF guard.
// (Stateful classes live in their own headers.)

#pragma once

#include <QMap>
#include <QString>
#include <QStringList>

#include <optional>

namespace ddg {

// ---------------------------------------------------------------------------
// Strings
// ---------------------------------------------------------------------------

bool    starts_with_ci(const QString& haystack, const QString& needle);
QString strip_slashes(QString s);
bool    contains(const QStringList& v, const QString& s);

// ---------------------------------------------------------------------------
// URLs (backed by QUrl)
// ---------------------------------------------------------------------------

/// RFC 3986 reference resolution (httpx.URL(base).join(location)).
QString url_join(const QString& base, const QString& location);

/// Lower-cased hostname, or "" if the URL has no host.
QString url_hostname(const QString& url);

/// Canonicalize a URL for use as a cache key (drop fragment, lowercase host).
QString normalize_cache_url(const QString& url);

// ---------------------------------------------------------------------------
// ref:// tokens
// ---------------------------------------------------------------------------

bool    is_ref_token(const QString& value);
QString unknown_ref_error(const QString& token);

// ---------------------------------------------------------------------------
// Retry-After
// ---------------------------------------------------------------------------

/// Parse a Retry-After header as seconds. Date-formatted values are ignored.
std::optional<double> retry_after_seconds(const QMap<QString, QString>& headers);

/// Sleep for Retry-After (capped at 30s) so 429s do not stall the tool.
double sleep_retry_after(const QMap<QString, QString>& headers,
                         double                       default_seconds = 2.0);

// ---------------------------------------------------------------------------
// SSRF guard
// ---------------------------------------------------------------------------

/// Reject non-public fetch targets. Throws BlockedUrlError. Resolves the host
/// (blocking) and rejects loopback / private / link-local / reserved /
/// multicast / unspecified destinations.
void validate_public_url(const QString& url);

}  // namespace ddg
