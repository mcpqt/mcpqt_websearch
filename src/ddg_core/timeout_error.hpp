// SPDX-License-Identifier: AGPL-3.0-only
//
// Request timeout (httpx.TimeoutException).

#pragma once

#include "http_error.hpp"

namespace ddg {

struct TimeoutError : HttpError {
    using HttpError::HttpError;
};

}  // namespace ddg
