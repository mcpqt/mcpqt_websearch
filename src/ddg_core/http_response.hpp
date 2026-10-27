// SPDX-License-Identifier: AGPL-3.0-only
//
// A single outbound HTTP response.

#pragma once

#include <QMap>
#include <QString>

namespace ddg {

struct HttpResponse {
    int                    status = 0;
    QString                body;
    QMap<QString, QString> headers;   // lower-cased keys
    QString                location;  // == headers["location"]

    [[nodiscard]] QString header(const QString& name) const {
        return headers.value(name);
    }
};

}  // namespace ddg
