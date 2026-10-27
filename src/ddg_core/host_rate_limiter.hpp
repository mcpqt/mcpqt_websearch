// SPDX-License-Identifier: AGPL-3.0-only
//
// Per-host limiter so one site cannot spend the whole fetch quota.

#pragma once

#include "rate_limiter.hpp"

#include <QMap>
#include <QMutex>
#include <QString>

#include <memory>

namespace ddg {

class HostRateLimiter {
public:
    HostRateLimiter(QString strategy, int requests_per_minute);

    void acquire(const QString& url);

private:
    QString                                         m_strategy;
    int                                             m_rpm;
    QMutex                                          m_mutex;
    QMap<QString, std::shared_ptr<RateLimiterBase>> m_limiters;
};

}  // namespace ddg
