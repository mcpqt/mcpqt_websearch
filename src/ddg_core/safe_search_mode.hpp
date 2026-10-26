// SPDX-License-Identifier: AGPL-3.0-only
//
// server.py SafeSearchMode.

#pragma once

#include <QString>

#include <optional>

namespace ddg {

enum class SafeSearchMode { Strict, Moderate, Off };

QString safe_search_value(SafeSearchMode m);  // "1" / "-1" / "-2"
QString safe_search_name(SafeSearchMode m);   // "STRICT" / "MODERATE" / "OFF"
std::optional<SafeSearchMode> safe_search_from_name(const QString& name);

}  // namespace ddg
