// SPDX-License-Identifier: Apache-2.0
//
// Request timeout (httpx.TimeoutException).

#pragma once

#include "http_error.hpp"

namespace ddg {

struct TimeoutError : HttpError {
    using HttpError::HttpError;
};

}  // namespace ddg
