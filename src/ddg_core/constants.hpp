// SPDX-License-Identifier: AGPL-3.0-only
//
// Shared constants. Mirrors the module-level constants in server.py.

#pragma once

#include <QString>
#include <QStringList>

namespace ddg {

inline const QString kRefScheme               = "ref://";
inline constexpr int kDefaultRefUrlThreshold  = 120;

// server.py: SUPPORTED_RATE_STRATEGIES
inline const QStringList kSupportedRateStrategies = {"sliding",
                                                     "token_bucket"};

// There is only one fetch backend; the actual transport is webio.
inline const QString kCompiledFetchBackend = "qt";

// server.py: SUPPORTED_PARSE_MODES
inline const QStringList kSupportedParseModes = {"text",
                                                 "raw",
                                                 "markdown"};

}  // namespace ddg
