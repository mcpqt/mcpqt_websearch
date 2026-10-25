// SPDX-License-Identifier: AGPL-3.0-only

#include "ttl_cache.hpp"

namespace ddg {

TTLCache::TTLCache(double ttl_seconds, int max_entries)
    : ttl_(ttl_seconds < 0 ? 0 : ttl_seconds),
      max_entries_(max_entries < 0 ? 0 : max_entries) {}

std::size_t TTLCache::size() const {
    std::lock_guard<std::mutex> lk(mu_);
    return store_.size();
}

std::optional<std::string> TTLCache::get(const std::string& key) {
    if (!enabled()) return std::nullopt;
    std::lock_guard<std::mutex> lk(mu_);
    auto it = store_.find(key);
    if (it == store_.end()) return std::nullopt;
    if (Clock::now() >= it->second.expires) {
        order_.erase(it->second.pos);
        store_.erase(it);
        return std::nullopt;
    }
    order_.splice(order_.end(), order_, it->second.pos);  // mark most-recently-used
    return it->second.value;
}

void TTLCache::set(const std::string& key, const std::string& value) {
    if (!enabled()) return;
    std::lock_guard<std::mutex> lk(mu_);
    const Time now = Clock::now();

    // Purge expired entries.
    for (auto it = store_.begin(); it != store_.end();) {
        if (now >= it->second.expires) {
            order_.erase(it->second.pos);
            it = store_.erase(it);
        } else {
            ++it;
        }
    }

    auto it = store_.find(key);
    if (it != store_.end()) {
        order_.erase(it->second.pos);
        store_.erase(it);
    } else if (static_cast<int>(store_.size()) >= max_entries_) {
        if (!order_.empty()) {
            store_.erase(order_.front());
            order_.pop_front();
        }
    }
    order_.push_back(key);
    store_[key] = Entry{now + std::chrono::duration_cast<Clock::duration>(
                                  std::chrono::duration<double>(ttl_)),
                        value, std::prev(order_.end())};
}

}  // namespace ddg
