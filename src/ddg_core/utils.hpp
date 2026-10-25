// SPDX-License-Identifier: AGPL-3.0-only
//
// Free-function utilities shared across the port: small string helpers, URL
// helpers, ref:// token helpers, Retry-After parsing, and the SSRF guard.
// (Stateful classes live in their own headers.)

#pragma once

#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ddg {

// ---------------------------------------------------------------------------
// Strings
// ---------------------------------------------------------------------------

std::string trim(std::string s);
std::string to_lower(std::string s);
bool        starts_with_ci(const std::string& haystack, const std::string& needle);
std::string strip_slashes(std::string s);
bool        contains(const std::vector<std::string>& v, const std::string& s);

/// Collapse runs of whitespace to single spaces, dropping blank lines.
std::string collapse_whitespace(const std::string& text);

// ---------------------------------------------------------------------------
// URLs (backed by QUrl)
// ---------------------------------------------------------------------------

/// RFC 3986 reference resolution (httpx.URL(base).join(location)).
std::string url_join(const std::string& base, const std::string& location);

/// application/x-www-form-urlencoded encoding of a key/value map.
std::string url_form_encode(const std::map<std::string, std::string>& form);

/// Lower-cased hostname, or "" if the URL has no host.
std::string url_hostname(const std::string& url);

/// Canonicalize a URL for use as a cache key (drop fragment, lowercase host).
std::string normalize_cache_url(const std::string& url);

// ---------------------------------------------------------------------------
// ref:// tokens
// ---------------------------------------------------------------------------

bool        is_ref_token(const std::string& value);
std::string unknown_ref_error(const std::string& token);

// ---------------------------------------------------------------------------
// Retry-After
// ---------------------------------------------------------------------------

/// Parse a Retry-After header as seconds. Date-formatted values are ignored.
std::optional<double> retry_after_seconds(
    const std::unordered_map<std::string, std::string>& headers);

/// Sleep for Retry-After (capped at 30s) so 429s do not stall the tool.
double sleep_retry_after(const std::unordered_map<std::string, std::string>& headers,
                         double                                            default_seconds = 2.0);

// ---------------------------------------------------------------------------
// SSRF guard
// ---------------------------------------------------------------------------

/// Reject non-public fetch targets. Throws BlockedUrlError. Resolves the host
/// (blocking) and rejects loopback / private / link-local / reserved /
/// multicast / unspecified destinations.
void validate_public_url(const std::string& url);

}  // namespace ddg
