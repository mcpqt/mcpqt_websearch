// SPDX-License-Identifier: AGPL-3.0-only
// SPDX-License-Identifier: Apache-2.0

#include "context.hpp"

namespace ddg {

namespace {
// Keep these in sync with mcp::LogLevel in the host's log.h.
constexpr int kInfo = 2;
constexpr int kError = 4;

void log_to(const Context::Logger& logger, int level, const std::string& message) {
    if (logger) logger(level, message);
}
}  // namespace

void Context::info(const std::string& message) const {
    log_to(logger_, kInfo, message);
}

void Context::error(const std::string& message) const {
    log_to(logger_, kError, message);
}

}  // namespace ddg
