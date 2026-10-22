// SPDX-License-Identifier: AGPL-3.0-only
// SPDX-License-Identifier: Apache-2.0
//
// Full HTML -> text extraction (server.py _html_to_text).

#pragma once

#include <string>

namespace ddg::html {

/// mode is "text" (default), "main" or "markdown".
std::string html_to_text(const std::string& html, const std::string& mode = "text");

}  // namespace ddg::html
