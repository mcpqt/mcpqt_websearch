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

    bool        enabled() const { return ttl_ > 0 && max_entries_ > 0; }
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

    double               ttl_;
    int                  max_entries_;
    mutable QMutex       mu_;
    QMap<QString, Entry> store_;
    QStringList          order_;  // front = LRU
};

}  // namespace ddg
