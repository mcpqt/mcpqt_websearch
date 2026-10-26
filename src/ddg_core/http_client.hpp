// SPDX-License-Identifier: AGPL-3.0-only
//
// Outbound HTTP client. The transport is injected by the plugin (webio) so the
// search/fetch logic stays independent of the concrete network stack.
//
// Exceptions mirror the httpx types server.py catches (HttpStatusError,
// ConnectError, TimeoutError).

#pragma once

#include "http_response.hpp"

#include <functional>
#include <map>
#include <string>

namespace ddg {

/// One outbound request. `form` is used for application/x-www-form-urlencoded
/// POSTs; `headers` applies to both methods.
struct HttpRequest {
    bool                               is_post = false;
    std::string                        url;
    std::map<std::string, std::string> headers;
    std::map<std::string, std::string> form;
    int                                timeout_ms = 30000;
};

/// Synchronous transport supplied by the plugin. It may throw TimeoutError,
/// ConnectError, or HttpError.
using Transport = std::function<HttpResponse(const HttpRequest&)>;

class HttpClient {
public:
    explicit HttpClient(Transport transport = {});

    /// GET without following redirects.
    HttpResponse get(const std::string&                       url,
                     const std::map<std::string, std::string>& headers    = {},
                     int                                       timeout_ms = 30000);

    /// application/x-www-form-urlencoded POST (the DuckDuckGo search call).
    HttpResponse post_form(const std::string&                       url,
                           const std::map<std::string, std::string>& form,
                           const std::map<std::string, std::string>& headers    = {},
                           int                                       timeout_ms = 30000);

private:
    HttpResponse perform(HttpRequest request);

    Transport transport_;
};

}  // namespace ddg
