// SPDX-License-Identifier: AGPL-3.0-only
//
// server.py LinkRegistry: maps long result URLs to short ref:// tokens.

#pragma once

#include <QMap>
#include <QMutex>
#include <QString>
#include <QStringList>

#include <cstddef>

namespace ddg {

class LinkRegistry {
public:
    explicit LinkRegistry(std::size_t max_entries = 2048);

    std::size_t size() const;
    void        clear();

    /// Register `url` and return its `ref://<id>` token.
    QString shorten(const QString& url);

    /// Resolve a `ref://<id>` token (or bare id). Returns a null QString if
    /// unknown.
    QString resolve(const QString& token);

private:
    struct Entry {
        QString url;
    };

    void evict_lru_locked();

    std::size_t          m_max_entries;
    mutable QMutex       m_mutex;
    QMap<QString, Entry> m_map;
    QStringList          m_order;  // front = LRU
};

}  // namespace ddg
