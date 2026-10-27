// SPDX-License-Identifier: AGPL-3.0-only

#include "rate_limiter.hpp"

#include "error.hpp"

#include <QMutexLocker>

#include <algorithm>
#include <thread>

namespace ddg {

SlidingRateLimiter::SlidingRateLimiter(int requests_per_minute)
    : m_rpm(requests_per_minute < 1 ? 1 : requests_per_minute) {}

void SlidingRateLimiter::purge_locked(Time now) {
    while (!m_requests.empty() && now - m_requests.front() >= std::chrono::minutes(1)) {
        m_requests.pop_front();
    }
}

void SlidingRateLimiter::acquire() {
    while (true) {
        double wait_seconds = 0.0;
        {
            QMutexLocker<QMutex> locker(&m_mutex);
            const Time           now = Clock::now();
            purge_locked(now);
            if (static_cast<int>(m_requests.size()) < m_rpm) {
                m_requests.push_back(now);
                return;
            }
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::duration<double>>(
                    now - m_requests.front()).count();
            wait_seconds = 60.0 - elapsed;
        }
        if (wait_seconds > 0) {
            std::this_thread::sleep_for(std::chrono::duration<double>(wait_seconds));
        } else {
            // Oldest entry is at/just past the window but still listed (clock
            // resolution pile-up): drop it so we cannot spin on sleep(0).
            QMutexLocker<QMutex> locker(&m_mutex);
            if (!m_requests.empty()) m_requests.pop_front();
        }
    }
}

bool SlidingRateLimiter::idle() {
    QMutexLocker<QMutex> locker(&m_mutex);
    purge_locked(Clock::now());
    return m_requests.empty();
}

TokenBucketLimiter::TokenBucketLimiter(int requests_per_minute, std::optional<double> burst)
    : m_rpm(requests_per_minute < 1 ? 1 : requests_per_minute),
      m_rate(static_cast<double>(m_rpm) / 60.0),
      m_burst(burst.value_or(static_cast<double>(m_rpm))),
      m_tokens(m_burst) {}

void TokenBucketLimiter::refill_locked() {
    const Time now = Clock::now();
    const double elapsed =
        std::chrono::duration_cast<std::chrono::duration<double>>(now - m_updated).count();
    m_updated = now;
    m_tokens  = std::min(m_burst, m_tokens + elapsed * m_rate);
}

bool TokenBucketLimiter::idle() {
    QMutexLocker<QMutex> locker(&m_mutex);
    refill_locked();
    return m_tokens >= m_burst;
}

void TokenBucketLimiter::acquire() {
    double wait_seconds = 0.0;
    {
        QMutexLocker<QMutex> locker(&m_mutex);
        refill_locked();
        if (m_tokens < 1.0) {
            wait_seconds = m_rate > 0 ? (1.0 - m_tokens) / m_rate : 1.0;
        }
        m_tokens -= 1.0;  // Python decrements even when going negative.
    }
    if (wait_seconds > 0) {
        std::this_thread::sleep_for(std::chrono::duration<double>(wait_seconds));
    }
}

std::unique_ptr<RateLimiterBase> make_rate_limiter(const QString& strategy,
                                                   int            requests_per_minute) {
    if (strategy == "token_bucket") {
        return std::make_unique<TokenBucketLimiter>(requests_per_minute);
    }
    if (strategy == "sliding") {
        return std::make_unique<SlidingRateLimiter>(requests_per_minute);
    }
    throw InvalidArgument(
        QString("Unknown rate-limit strategy '%1'. Supported: sliding, token_bucket")
            .arg(strategy));
}

}  // namespace ddg
