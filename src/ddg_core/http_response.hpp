// SPDX-License-Identifier: AGPL-3.0-only
//
// A single outbound HTTP response.

#pragma once

#include <string>
#include <unordered_map>

namespace ddg {

struct HttpResponse {
    int                                          status = 0;
    std::string                                  body;
    std::unordered_map<std::string, std::string> headers;  // lower-cased keys
    std::string                                  location;  // == headers["location"]

    [[nodiscard]] std::string header(const std::string& name) const {
        auto it = headers.find(name);
        return it == headers.end() ? std::string{} : it->second;
    }
};

}  // namespace ddg
