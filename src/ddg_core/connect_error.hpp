// SPDX-License-Identifier: AGPL-3.0-only
//
// Transport-level connection failure (httpx.ConnectError).

#pragma once

#include "http_error.hpp"

namespace ddg {

struct ConnectError : HttpError {
    using HttpError::HttpError;
};

}  // namespace ddg
