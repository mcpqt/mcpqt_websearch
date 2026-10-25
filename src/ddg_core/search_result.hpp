// SPDX-License-Identifier: AGPL-3.0-only
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
