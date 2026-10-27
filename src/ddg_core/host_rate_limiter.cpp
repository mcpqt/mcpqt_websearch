// SPDX-License-Identifier: AGPL-3.0-only

#include "host_rate_limiter.hpp"

#include "rate_limiter.hpp"
#include "utils.hpp"

#include <QMutexLocker>
#include <QUrl>

#include <memory>

namespace ddg {

HostRateLimiter::HostRateLimiter(QString strategy, int requests_per_minute)
    : m_strategy(std::move(strategy)), m_rpm(requests_per_minute) {}

void HostRateLimiter::acquire(const QString& url) {
    QString host = QUrl(url).host().toLower();
    if (host.isEmpty()) host = "unknown";

    RateLimiterBase* limiter = nullptr;
    {
        QMutexLocker<QMutex> locker(&m_mutex);
        // Drop limiters for hosts that have gone quiet so the map does not grow
        // one entry per distinct host for the life of the server.
        for (auto it = m_limiters.begin(); it != m_limiters.end();) {
            if (it.key() != host && it.value()->idle()) {
                it = m_limiters.erase(it);
            } else {
                ++it;
            }
        }
        auto it = m_limiters.find(host);
        if (it == m_limiters.end()) {
            std::shared_ptr<RateLimiterBase> owned = make_rate_limiter(m_strategy, m_rpm);
            it = m_limiters.insert(host, owned);
        }
        limiter = it.value().get();
    }
    limiter->acquire();
}

}  // namespace ddg
