// SPDX-License-Identifier: AGPL-3.0-only
// SPDX-License-Identifier: Apache-2.0
//
// server.py SearchResult.

#pragma once

#include <string>

namespace ddg {

struct SearchResult {
    std::string title;
    std::string link;
    std::string snippet;
    int         position = 0;
};

}  // namespace ddg
