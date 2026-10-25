// SPDX-License-Identifier: AGPL-3.0-only
//
// Base class for outbound HTTP failures.

#pragma once

#include <stdexcept>

namespace ddg {

struct HttpError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

}  // namespace ddg
