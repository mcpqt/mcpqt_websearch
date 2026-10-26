// SPDX-License-Identifier: AGPL-3.0-only
//
// QString-backed exception base. std::exception's what() must be a const char*,
// so the UTF-8 form is cached alongside the QString message.

#pragma once

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

}  // namespace ddg
