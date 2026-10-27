// SPDX-License-Identifier: AGPL-3.0-only

#include "host_rate_limiter.hpp"

#include "rate_limiter.hpp"
#include "utils.hpp"

#include <QMutexLocker>
#include <QUrl>

#include <memory>

namespace ddg {

HostRateLimiter::HostRateLimiter(QString strategy, int requests_per_minute)
    : strategy_(std::move(strategy)), rpm_(requests_per_minute) {}

void HostRateLimiter::acquire(const QString& url) {
    QString host = QUrl(url).host().toLower();
    if (host.isEmpty()) host = "unknown";

    RateLimiterBase* limiter = nullptr;
    {
        QMutexLocker<QMutex> locker(&mu_);
        // Drop limiters for hosts that have gone quiet so the map does not grow
        // one entry per distinct host for the life of the server.
        for (auto it = limiters_.begin(); it != limiters_.end();) {
            if (it.key() != host && it.value()->idle()) {
                it = limiters_.erase(it);
            } else {
                ++it;
            }
        }
        auto it = limiters_.find(host);
        if (it == limiters_.end()) {
            std::shared_ptr<RateLimiterBase> owned = make_rate_limiter(strategy_, rpm_);
            it = limiters_.insert(host, owned);
        }
        limiter = it.value().get();
    }
    limiter->acquire();
}

}  // namespace ddg
