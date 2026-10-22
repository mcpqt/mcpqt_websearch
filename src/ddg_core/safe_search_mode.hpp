// SPDX-License-Identifier: Apache-2.0
//
// server.py SafeSearchMode.

#pragma once

#include <optional>
#include <string>

namespace ddg {

enum class SafeSearchMode { Strict, Moderate, Off };

const char* safe_search_value(SafeSearchMode m);  // "1" / "-1" / "-2"
const char* safe_search_name(SafeSearchMode m);   // "STRICT" / "MODERATE" / "OFF"
std::optional<SafeSearchMode> safe_search_from_name(const std::string& name);

}  // namespace ddg
