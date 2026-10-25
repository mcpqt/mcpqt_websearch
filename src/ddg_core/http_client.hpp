// SPDX-License-Identifier: AGPL-3.0-only
//
// Outbound HTTP client. The transport implementation is selected at configure
// time (see DDG_HTTP_BACKEND): either QNetworkAccessManager ("qt") or the
// system `curl` binary ("curl"). Only the matching .cpp is compiled, so this
// header stays free of Qt/process specifics.
//
// Exceptions mirror the httpx types server.py catches (HttpStatusError,
// ConnectError, TimeoutError).

#pragma once

#include "http_response.hpp"
#include "tls_verify.hpp"

#include <map>
#include <string>

namespace ddg {

class HttpClient {
public:
    explicit HttpClient(TlsVerify verify);

    /// GET without following redirects.
    HttpResponse get(const std::string&                       url,
                     const std::map<std::string, std::string>& headers    = {},
                     int                                       timeout_ms = 30000);

    /// application/x-www-form-urlencoded POST (the DuckDuckGo search call).
    HttpResponse post_form(const std::string&                       url,
                           const std::map<std::string, std::string>& form,
                           const std::map<std::string, std::string>& headers    = {},
                           int                                       timeout_ms = 30000);

    /// The compiled-in backend name: "qt" or "curl".
    static const char* backend_name();

private:
    HttpResponse perform(const std::string&                       method,
                         const std::string&                       url,
                         const std::map<std::string, std::string>& headers,
                         const std::string&                       body,
                         int                                      timeout_ms);

    TlsVerify verify_;
};

}  // namespace ddg
