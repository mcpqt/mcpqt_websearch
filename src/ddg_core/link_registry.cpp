// SPDX-License-Identifier: AGPL-3.0-only

#include "link_registry.hpp"

#include "constants.hpp"
#include "utils.hpp"

#include <QCryptographicHash>
#include <QMutexLocker>

namespace ddg {

LinkRegistry::LinkRegistry(std::size_t max_entries)
    : max_entries_(max_entries == 0 ? 1 : max_entries) {}

std::size_t LinkRegistry::size() const {
    QMutexLocker<QMutex> locker(&mu_);
    return static_cast<std::size_t>(map_.size());
}

void LinkRegistry::clear() {
    QMutexLocker<QMutex> locker(&mu_);
    map_.clear();
    order_.clear();
}

QString LinkRegistry::shorten(const QString& url) {
    // sha256 hex of the URL, exactly as Python's hashlib.sha256(url).hexdigest().
    const QByteArray digest =
        QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha256).toHex();

    QMutexLocker<QMutex> locker(&mu_);
    QString              key;
    for (int length = 8; length <= digest.size(); ++length) {
        key      = QString::fromLatin1(digest.left(length));
        auto it  = map_.constFind(key);
        if (it == map_.cend() || it.value().url == url) break;
    }

    auto it = map_.find(key);
    if (it != map_.end()) {
        order_.removeAll(key);
        order_.append(key);  // move_to_end
    } else {
        order_.append(key);
        map_.insert(key, Entry{url});
        while (static_cast<std::size_t>(map_.size()) > max_entries_) evict_lru_locked();
    }
    return kRefScheme + key;
}

QString LinkRegistry::resolve(const QString& token) {
    QString key = trim(token);
    if (starts_with_ci(key, kRefScheme)) key = key.mid(kRefScheme.size());
    key = to_lower(strip_slashes(trim(key)));

    QMutexLocker<QMutex> locker(&mu_);
    auto                 it = map_.find(key);
    if (it == map_.end()) return QString();
    order_.removeAll(key);
    order_.append(key);
    return it.value().url;
}

void LinkRegistry::evict_lru_locked() {
    if (order_.isEmpty()) return;
    map_.remove(order_.takeFirst());
}

}  // namespace ddg
