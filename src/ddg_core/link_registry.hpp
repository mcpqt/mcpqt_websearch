// SPDX-License-Identifier: Apache-2.0
//
// server.py LinkRegistry: maps long result URLs to short ref:// tokens.

#pragma once

#include <cstddef>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace ddg {

class LinkRegistry {
public:
    explicit LinkRegistry(std::size_t max_entries = 2048);

    std::size_t size() const;
    void        clear();

    /// Register `url` and return its `ref://<id>` token.
    std::string shorten(const std::string& url);

    /// Resolve a `ref://<id>` token (or bare id). Returns nullopt if unknown.
    std::optional<std::string> resolve(const std::string& token);

private:
    struct Entry {
        std::string                     url;
        std::list<std::string>::iterator pos;
    };

    void evict_lru_locked();

    std::size_t                            max_entries_;
    mutable std::mutex                     mu_;
    std::unordered_map<std::string, Entry> map_;
    std::list<std::string>                 order_;  // front = LRU
};

}  // namespace ddg
