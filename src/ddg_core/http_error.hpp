// SPDX-License-Identifier: AGPL-3.0-only
//
// Base class for outbound HTTP failures.

#pragma once

#include "error.hpp"

namespace ddg {

struct HttpError : Error {
    using Error::Error;
};

}  // namespace ddg
