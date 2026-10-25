// SPDX-License-Identifier: AGPL-3.0-only
//
// server.py TTLCache: the fetch_content parsed-page cache.

#pragma once

#include <chrono>
#include <cstddef>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace ddg {

class TTLCache {
public:
    TTLCache(double ttl_seconds, int max_entries);

    bool        enabled() const { return ttl_ > 0 && max_entries_ > 0; }
    std::size_t size() const;

    std::optional<std::string> get(const std::string& key);
    void                       set(const std::string& key, const std::string& value);

private:
    using Clock = std::chrono::steady_clock;
    using Time  = Clock::time_point;

    struct Entry {
        Time                            expires;
        std::string                     value;
        std::list<std::string>::iterator pos;
    };

    double                                 ttl_;
    int                                    max_entries_;
    mutable std::mutex                     mu_;
    std::unordered_map<std::string, Entry> store_;
    std::list<std::string>                 order_;  // front = LRU
};

}  // namespace ddg
