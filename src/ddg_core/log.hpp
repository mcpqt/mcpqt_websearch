// SPDX-License-Identifier: AGPL-3.0-only
//
// Host diagnostic logging callback. Level values match mcp::LogLevel in the
// host's log.h (2 = info, 4 = error).

#pragma once

#include <functional>
#include <string>

namespace ddg {

using LogFn = std::function<void(int level, const std::string& message)>;

inline void log_info(const LogFn& log, const std::string& message) {
    if (log) log(2, message);
}

inline void log_error(const LogFn& log, const std::string& message) {
    if (log) log(4, message);
}

}  // namespace ddg
