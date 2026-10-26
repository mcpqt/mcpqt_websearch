// SPDX-License-Identifier: AGPL-3.0-only
//
// Outbound HTTP request/transport types. The transport is injected by the
// plugin (webio), so the search/fetch logic stays independent of the concrete
// network stack.

#pragma once

#include "http_response.hpp"

#include <QMap>
#include <QString>

#include <functional>

namespace ddg {

/// One outbound request. `form` is used for application/x-www-form-urlencoded
/// POSTs; `headers` applies to both methods.
struct HttpRequest {
    bool                   is_post = false;
    QString                url;
    QMap<QString, QString> headers;
    QMap<QString, QString> form;
    int                    timeout_ms = 30000;
};

/// Synchronous transport supplied by the plugin. It may throw TimeoutError,
/// ConnectError, or HttpError.
using Transport = std::function<HttpResponse(const HttpRequest&)>;

}  // namespace ddg
