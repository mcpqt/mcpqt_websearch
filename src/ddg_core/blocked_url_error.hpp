// SPDX-License-Identifier: AGPL-3.0-only
//
// Raised when a fetch target is not an allowed public http(s) destination.

#pragma once

#include <stdexcept>

namespace ddg {

struct BlockedUrlError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

}  // namespace ddg
