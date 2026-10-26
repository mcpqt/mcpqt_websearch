// SPDX-License-Identifier: AGPL-3.0-only
//
// Raised when a fetch target is not an allowed public http(s) destination.

#pragma once

#include "error.hpp"

namespace ddg {

struct BlockedUrlError : Error {
    using Error::Error;
};

}  // namespace ddg
