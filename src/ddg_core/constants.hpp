// SPDX-License-Identifier: AGPL-3.0-only
//
// Shared constants. Mirrors the module-level constants in server.py.

#pragma once

#include <string>
#include <vector>

namespace ddg {

inline constexpr const char* kRefScheme              = "ref://";
inline constexpr int         kDefaultRefUrlThreshold = 120;

// server.py: SUPPORTED_RATE_STRATEGIES
inline const std::vector<std::string> kSupportedRateStrategies = {"sliding", "token_bucket"};

// server.py: SUPPORTED_FETCH_BACKENDS. The Python originals are httpx/curl/auto;
// here "httpx" is renamed "qt" (QNetworkAccessManager) and "auto" is dropped.
// Both names are recognised at runtime; the actual transport is webio.
inline const std::vector<std::string> kSupportedFetchBackends = {"qt", "curl"};

// Advisory backend name; the actual transport is webio.
inline constexpr const char* kCompiledFetchBackend = "qt";

// server.py: SUPPORTED_PARSE_MODES
inline const std::vector<std::string> kSupportedParseModes = {"text", "main", "markdown"};

}  // namespace ddg
