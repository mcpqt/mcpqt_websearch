// SPDX-License-Identifier: AGPL-3.0-only
//
// server.py TTLCache: the fetch_content parsed-page cache.

#pragma once

#include <QMap>
#include <QMutex>
#include <QString>
#include <QStringList>

#include <chrono>
#include <cstddef>

namespace ddg {

class TTLCache {
public:
    TTLCache(double ttl_seconds, int max_entries);

    bool        enabled() const { return m_ttl > 0 && m_max_entries > 0; }
    std::size_t size() const;

    QString get(const QString& key);
    void    set(const QString& key, const QString& value);

private:
    using Clock = std::chrono::steady_clock;
    using Time  = Clock::time_point;

    struct Entry {
        Time    expires;
        QString value;
    };

    double               m_ttl;
    int                  m_max_entries;
    mutable QMutex       m_mutex;
    QMap<QString, Entry> m_store;
    QStringList          m_order;  // front = LRU
};

}  // namespace ddg
