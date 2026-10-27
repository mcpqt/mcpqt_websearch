// SPDX-License-Identifier: AGPL-3.0-only
//
// Shared exception types. std::exception's what() must be a const char*, so the
// UTF-8 form is cached alongside the QString message.

#pragma once

#include "http_response.hpp"

#include <QByteArray>
#include <QString>

#include <exception>
#include <utility>

namespace ddg {

class Error : public std::exception {
public:
    explicit Error(QString message)
        : m_message(std::move(message)), m_utf8(m_message.toUtf8()) {}

    const char* what() const noexcept override { return m_utf8.constData(); }

    [[nodiscard]] const QString& message() const noexcept { return m_message; }

private:
    QString    m_message;
    QByteArray m_utf8;
};

struct InvalidArgument : Error {
    using Error::Error;
};

/// Raised when a fetch target is not an allowed public http(s) destination.
struct BlockedUrlError : Error {
    using Error::Error;
};

/// Base class for outbound HTTP failures.
struct HttpError : Error {
    using Error::Error;
};

/// Transport-level connection failure (httpx.ConnectError).
struct ConnectError : HttpError {
    using HttpError::HttpError;
};

/// Request timeout (httpx.TimeoutException).
struct TimeoutError : HttpError {
    using HttpError::HttpError;
};

/// Non-2xx status (the analogue of httpx.HTTPStatusError).
struct HttpStatusError : HttpError {
    int          status;
    HttpResponse response;

    HttpStatusError(int s, HttpResponse r)
        : HttpError(QString("HTTP %1").arg(s)),
          status(s),
          response(std::move(r)) {}
};

}  // namespace ddg
