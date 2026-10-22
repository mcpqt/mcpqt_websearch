// SPDX-License-Identifier: Apache-2.0

#include "link_registry.hpp"

#include "constants.hpp"
#include "utils.hpp"

#include <QCryptographicHash>

namespace ddg {

LinkRegistry::LinkRegistry(std::size_t max_entries)
    : max_entries_(max_entries == 0 ? 1 : max_entries) {}

std::size_t LinkRegistry::size() const {
    std::lock_guard<std::mutex> lk(mu_);
    return map_.size();
}

void LinkRegistry::clear() {
    std::lock_guard<std::mutex> lk(mu_);
    map_.clear();
    order_.clear();
}

std::string LinkRegistry::shorten(const std::string& url) {
    // sha256 hex of the URL, exactly as Python's hashlib.sha256(url).hexdigest().
    const QByteArray digest =
        QCryptographicHash::hash(QByteArray::fromStdString(url),
                                 QCryptographicHash::Sha256).toHex();

    std::lock_guard<std::mutex> lk(mu_);
    std::string key;
    for (int length = 8; length <= digest.size(); ++length) {
        key = digest.left(length).toStdString();
        auto it = map_.find(key);
        if (it == map_.end() || it->second.url == url) break;
    }

    auto it = map_.find(key);
    if (it != map_.end()) {
        order_.splice(order_.end(), order_, it->second.pos);  // move_to_end
    } else {
        order_.push_back(key);
        map_[key] = Entry{url, std::prev(order_.end())};
        while (map_.size() > max_entries_) evict_lru_locked();
    }
    return std::string(kRefScheme) + key;
}

std::optional<std::string> LinkRegistry::resolve(const std::string& token) {
    std::string key = trim(token);
    if (starts_with_ci(key, kRefScheme)) key = key.substr(std::string(kRefScheme).size());
    key = to_lower(strip_slashes(trim(key)));

    std::lock_guard<std::mutex> lk(mu_);
    auto it = map_.find(key);
    if (it == map_.end()) return std::nullopt;
    order_.splice(order_.end(), order_, it->second.pos);
    return it->second.url;
}

void LinkRegistry::evict_lru_locked() {
    if (order_.empty()) return;
    map_.erase(order_.front());
    order_.pop_front();
}

}  // namespace ddg
