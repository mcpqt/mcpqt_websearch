// SPDX-License-Identifier: AGPL-3.0-only

#include "safe_search_mode.hpp"

#include "utils.hpp"

namespace ddg {

QString safe_search_value(SafeSearchMode m) {
    switch (m) {
        case SafeSearchMode::Strict:   return QStringLiteral("1");
        case SafeSearchMode::Moderate: return QStringLiteral("-1");
        case SafeSearchMode::Off:      return QStringLiteral("-2");
    }
    return QStringLiteral("-1");
}

QString safe_search_name(SafeSearchMode m) {
    switch (m) {
        case SafeSearchMode::Strict:   return QStringLiteral("STRICT");
        case SafeSearchMode::Moderate: return QStringLiteral("MODERATE");
        case SafeSearchMode::Off:      return QStringLiteral("OFF");
    }
    return QStringLiteral("MODERATE");
}

std::optional<SafeSearchMode> safe_search_from_name(const QString& name) {
    const QString n = to_lower(trim(name));
    if (n == QStringLiteral("strict"))   return SafeSearchMode::Strict;
    if (n == QStringLiteral("moderate")) return SafeSearchMode::Moderate;
    if (n == QStringLiteral("off"))      return SafeSearchMode::Off;
    return std::nullopt;
}

}  // namespace ddg
