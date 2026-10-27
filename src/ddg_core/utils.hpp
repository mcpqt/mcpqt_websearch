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

QString strip_slashes(QString s);

// ---------------------------------------------------------------------------
// URLs (backed by QUrl)
// ---------------------------------------------------------------------------

/// RFC 3986 reference resolution (httpx.URL(base).join(location)).
QString url_join(const QString& base, const QString& location);

// ---------------------------------------------------------------------------
// ref:// tokens
// ---------------------------------------------------------------------------

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
