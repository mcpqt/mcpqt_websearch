// SPDX-License-Identifier: AGPL-3.0-only
//
// Factory for the configured rate-limit strategy.

#pragma once

#include "rate_limiter.hpp"

#include <memory>
#include <string>

namespace ddg {

std::unique_ptr<RateLimiterBase> make_rate_limiter(const std::string& strategy,
                                                   int                requests_per_minute);

}  // namespace ddg
