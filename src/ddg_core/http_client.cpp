// SPDX-License-Identifier: AGPL-3.0-only
//
// Thin adapter around the injected Transport. The network stack itself lives in
// the webio plugin (see webio_transport.cpp).

#include "http_client.hpp"

#include "http_error.hpp"

#include <utility>

namespace ddg {

HttpClient::HttpClient(TlsVerify, Transport transport)
    : transport_(std::move(transport)) {}

HttpResponse HttpClient::get(const std::string&                       url,
                             const std::map<std::string, std::string>& headers,
                             int                                       timeout_ms) {
    HttpRequest request;
    request.url        = url;
    request.headers    = headers;
    request.timeout_ms = timeout_ms;
    return perform(std::move(request));
}

HttpResponse HttpClient::post_form(const std::string&                       url,
                                   const std::map<std::string, std::string>& form,
                                   const std::map<std::string, std::string>& headers,
                                   int                                       timeout_ms) {
    HttpRequest request;
    request.is_post    = true;
    request.url        = url;
    request.headers    = headers;
    request.form       = form;
    request.timeout_ms = timeout_ms;
    return perform(std::move(request));
}

const char* HttpClient::backend_name() { return "qt"; }

HttpResponse HttpClient::perform(HttpRequest request) {
    if (!transport_) {
        throw HttpError("no HTTP transport configured");
    }
    return transport_(request);
}

}  // namespace ddg
