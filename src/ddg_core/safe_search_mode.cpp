// SPDX-License-Identifier: AGPL-3.0-only

#include "safe_search_mode.hpp"

#include "utils.hpp"

namespace ddg {

QString safe_search_value(SafeSearchMode m) {
    switch (m) {
        case SafeSearchMode::Strict:   return "1";
        case SafeSearchMode::Moderate: return "-1";
        case SafeSearchMode::Off:      return "-2";
    }
    return "-1";
}

QString safe_search_name(SafeSearchMode m) {
    switch (m) {
        case SafeSearchMode::Strict:   return "STRICT";
        case SafeSearchMode::Moderate: return "MODERATE";
        case SafeSearchMode::Off:      return "OFF";
    }
    return "MODERATE";
}

std::optional<SafeSearchMode> safe_search_from_name(const QString& name) {
    const QString n = to_lower(trim(name));
    if (n == "strict")   return SafeSearchMode::Strict;
    if (n == "moderate") return SafeSearchMode::Moderate;
    if (n == "off")      return SafeSearchMode::Off;
    return std::nullopt;
}

}  // namespace ddg
