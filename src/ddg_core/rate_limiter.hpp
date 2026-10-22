// SPDX-License-Identifier: AGPL-3.0-only
// SPDX-License-Identifier: Apache-2.0
//
// Common interface so HostRateLimiter can hold either strategy.

#pragma once

namespace ddg {

class RateLimiterBase {
public:
    virtual ~RateLimiterBase() = default;
    virtual void acquire() = 0;
    virtual bool idle()    = 0;
};

}  // namespace ddg
