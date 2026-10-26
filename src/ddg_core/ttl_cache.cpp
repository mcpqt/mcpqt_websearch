// SPDX-License-Identifier: AGPL-3.0-only

#include "ttl_cache.hpp"

#include <QMutexLocker>

namespace ddg {

TTLCache::TTLCache(double ttl_seconds, int max_entries)
    : ttl_(ttl_seconds < 0 ? 0 : ttl_seconds),
      max_entries_(max_entries < 0 ? 0 : max_entries) {}

std::size_t TTLCache::size() const {
    QMutexLocker<QMutex> locker(&mu_);
    return static_cast<std::size_t>(store_.size());
}

QString TTLCache::get(const QString& key) {
    if (!enabled()) return QString();
    QMutexLocker<QMutex> locker(&mu_);
    auto                 it = store_.find(key);
    if (it == store_.end()) return QString();
    if (Clock::now() >= it.value().expires) {
        order_.removeAll(key);
        store_.erase(it);
        return QString();
    }
    order_.removeAll(key);
    order_.append(key);  // mark most-recently-used
    return it.value().value;
}

void TTLCache::set(const QString& key, const QString& value) {
    if (!enabled()) return;
    QMutexLocker<QMutex> locker(&mu_);
    const Time           now = Clock::now();

    // Purge expired entries.
    for (auto it = store_.begin(); it != store_.end();) {
        if (now >= it.value().expires) {
            order_.removeAll(it.key());
            it = store_.erase(it);
        } else {
            ++it;
        }
    }

    if (store_.contains(key)) {
        order_.removeAll(key);
        store_.remove(key);
    } else if (static_cast<int>(store_.size()) >= max_entries_) {
        if (!order_.isEmpty()) store_.remove(order_.takeFirst());
    }
    order_.append(key);
    store_.insert(key, Entry{now + std::chrono::duration_cast<Clock::duration>(
                                     std::chrono::duration<double>(ttl_)),
                             value});
}

}  // namespace ddg
