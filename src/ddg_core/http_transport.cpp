// SPDX-License-Identifier: AGPL-3.0-only
//
// Free helpers that build an HttpRequest and hand it to the injected Transport.
// The network stack itself lives in the webio plugin (see webio_transport.cpp).

#include "http_transport.hpp"

#include "http_error.hpp"

namespace ddg {
namespace {

void ensure_transport(const Transport& transport) {
    if (!transport) {
        throw HttpError("no HTTP transport configured");
    }
}

}  // namespace

HttpResponse http_get(const Transport&                          transport,
                      const std::string&                        url,
                      const std::map<std::string, std::string>& headers,
                      int                                       timeout_ms) {
    ensure_transport(transport);
    HttpRequest request;
    request.url        = url;
    request.headers    = headers;
    request.timeout_ms = timeout_ms;
    return transport(request);
}

HttpResponse http_post_form(const Transport&                          transport,
                            const std::string&                        url,
                            const std::map<std::string, std::string>& form,
                            const std::map<std::string, std::string>& headers,
                            int                                       timeout_ms) {
    ensure_transport(transport);
    HttpRequest request;
    request.is_post    = true;
    request.url        = url;
    request.headers    = headers;
    request.form       = form;
    request.timeout_ms = timeout_ms;
    return transport(request);
}

}  // namespace ddg
