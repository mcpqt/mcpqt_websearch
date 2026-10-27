// SPDX-License-Identifier: AGPL-3.0-only
//
// Rate-limiter interface and the two supported strategies. HostRateLimiter
// holds a RateLimiterBase so the configured strategy is interchangeable.

#pragma once

#include <QMutex>
#include <QString>

#include <chrono>
#include <list>
#include <memory>
#include <optional>

namespace ddg {

class RateLimiterBase {
public:
    virtual ~RateLimiterBase() = default;
    virtual void acquire() = 0;
    virtual bool idle()    = 0;
};

/// Sliding-window limiter (historical default): at most N requests per 60s.
class SlidingRateLimiter : public RateLimiterBase {
public:
    explicit SlidingRateLimiter(int requests_per_minute);

    void acquire() override;
    bool idle() override;

private:
    using Clock = std::chrono::steady_clock;
    using Time  = Clock::time_point;

    void purge_locked(Time now);

    int             m_rpm;
    QMutex          m_mutex;
    std::list<Time> m_requests;
};

/// Token-bucket limiter: allows a short burst, then smooths to `rpm`.
class TokenBucketLimiter : public RateLimiterBase {
public:
    explicit TokenBucketLimiter(int                   requests_per_minute,
                                std::optional<double> burst = std::nullopt);

    void acquire() override;
    bool idle() override;

private:
    using Clock = std::chrono::steady_clock;
    using Time  = Clock::time_point;

    void refill_locked();

    int      m_rpm;
    double   m_rate;
    double   m_burst;
    double   m_tokens;
    Time     m_updated = Clock::now();
    QMutex   m_mutex;
};

/// Create the configured rate-limiter strategy. Throws InvalidArgument for an
/// unknown strategy name.
std::unique_ptr<RateLimiterBase> make_rate_limiter(const QString& strategy,
                                                   int            requests_per_minute);

}  // namespace ddg
