// SPDX-License-Identifier: AGPL-3.0-only

#include "ttl_cache.hpp"

#include <QMutexLocker>

namespace ddg {

TTLCache::TTLCache(double ttl_seconds, int max_entries)
    : m_ttl(ttl_seconds < 0 ? 0 : ttl_seconds),
      m_max_entries(max_entries < 0 ? 0 : max_entries) {}

std::size_t TTLCache::size() const {
    QMutexLocker<QMutex> locker(&m_mutex);
    return static_cast<std::size_t>(m_store.size());
}

QString TTLCache::get(const QString& key) {
    if (!enabled()) return QString();
    QMutexLocker<QMutex> locker(&m_mutex);
    auto                 it = m_store.find(key);
    if (it == m_store.end()) return QString();
    if (Clock::now() >= it.value().expires) {
        m_order.removeAll(key);
        m_store.erase(it);
        return QString();
    }
    m_order.removeAll(key);
    m_order.append(key);  // mark most-recently-used
    return it.value().value;
}

void TTLCache::set(const QString& key, const QString& value) {
    if (!enabled()) return;
    QMutexLocker<QMutex> locker(&m_mutex);
    const Time           now = Clock::now();

    // Purge expired entries.
    for (auto it = m_store.begin(); it != m_store.end();) {
        if (now >= it.value().expires) {
            m_order.removeAll(it.key());
            it = m_store.erase(it);
        } else {
            ++it;
        }
    }

    if (m_store.contains(key)) {
        m_order.removeAll(key);
        m_store.remove(key);
    } else if (static_cast<int>(m_store.size()) >= m_max_entries) {
        if (!m_order.isEmpty()) m_store.remove(m_order.takeFirst());
    }
    m_order.append(key);
    m_store.insert(key, Entry{now + std::chrono::duration_cast<Clock::duration>(
                                     std::chrono::duration<double>(m_ttl)),
                             value});
}

}  // namespace ddg
