// SPDX-License-Identifier: AGPL-3.0-only
//
// Non-2xx status (the analogue of httpx.HTTPStatusError).

#pragma once

#include "http_error.hpp"
#include "http_response.hpp"

#include <string>
#include <utility>

namespace ddg {

struct HttpStatusError : HttpError {
    int          status;
    HttpResponse response;

    HttpStatusError(int s, HttpResponse r)
        : HttpError("HTTP " + std::to_string(s)), status(s), response(std::move(r)) {}
};

}  // namespace ddg
