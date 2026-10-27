// SPDX-License-Identifier: AGPL-3.0-only

#include "link_registry.hpp"

#include "constants.hpp"
#include "utils.hpp"

#include <QCryptographicHash>
#include <QMutexLocker>

namespace ddg {

LinkRegistry::LinkRegistry(std::size_t max_entries)
    : m_max_entries(max_entries == 0 ? 1 : max_entries) {}

std::size_t LinkRegistry::size() const {
    QMutexLocker<QMutex> locker(&m_mutex);
    return static_cast<std::size_t>(m_map.size());
}

void LinkRegistry::clear() {
    QMutexLocker<QMutex> locker(&m_mutex);
    m_map.clear();
    m_order.clear();
}

QString LinkRegistry::shorten(const QString& url) {
    // sha256 hex of the URL, exactly as Python's hashlib.sha256(url).hexdigest().
    const QByteArray digest =
        QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha256).toHex();

    QMutexLocker<QMutex> locker(&m_mutex);
    QString              key;
    for (int length = 8; length <= digest.size(); ++length) {
        key      = QString::fromLatin1(digest.left(length));
        auto it  = m_map.constFind(key);
        if (it == m_map.cend() || it.value().url == url) break;
    }

    auto it = m_map.find(key);
    if (it != m_map.end()) {
        m_order.removeAll(key);
        m_order.append(key);  // move_to_end
    } else {
        m_order.append(key);
        m_map.insert(key, Entry{url});
        while (static_cast<std::size_t>(m_map.size()) > m_max_entries) evict_lru_locked();
    }
    return kRefScheme + key;
}

QString LinkRegistry::resolve(const QString& token) {
    QString key = token.trimmed();
    if (key.startsWith(kRefScheme, Qt::CaseInsensitive)) key = key.mid(kRefScheme.size());
    key = strip_slashes(key.trimmed()).toLower();

    QMutexLocker<QMutex> locker(&m_mutex);
    auto                 it = m_map.find(key);
    if (it == m_map.end()) return QString();
    m_order.removeAll(key);
    m_order.append(key);
    return it.value().url;
}

void LinkRegistry::evict_lru_locked() {
    if (m_order.isEmpty()) return;
    m_map.remove(m_order.takeFirst());
}

}  // namespace ddg
