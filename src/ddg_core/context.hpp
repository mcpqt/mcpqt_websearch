// SPDX-License-Identifier: AGPL-3.0-only
// SPDX-License-Identifier: Apache-2.0
//
// server.py's `ctx` (Context): the logging surface tool handlers use. In the
// plugin build this forwards to the host's diagnostic logging system.

#pragma once

#include <functional>
#include <string>
#include <utility>

namespace ddg {

class Context {
public:
    /// Receives an mcp::LogLevel-style integer (2=info, 4=error) and the
    /// message. The host supplies this callback when the plugin is loaded.
    using Logger = std::function<void(int level, const std::string& message)>;

    Context() = default;
    explicit Context(Logger logger) : logger_(std::move(logger)) {}

    void info(const std::string& message) const;
    void error(const std::string& message) const;

private:
    Logger logger_;
};

}  // namespace ddg
