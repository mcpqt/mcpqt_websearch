// SPDX-License-Identifier: AGPL-3.0-only

#include "http_client.hpp"

#include "utils.hpp"

namespace ddg {

HttpClient::HttpClient(TlsVerify verify) : verify_(std::move(verify)) {}

HttpResponse HttpClient::get(const std::string&                       url,
                             const std::map<std::string, std::string>& headers,
                             int                                       timeout_ms) {
    return perform("GET", url, headers, {}, timeout_ms);
}

HttpResponse HttpClient::post_form(const std::string&                       url,
                                   const std::map<std::string, std::string>& form,
                                   const std::map<std::string, std::string>& headers,
                                   int                                       timeout_ms) {
    std::map<std::string, std::string> h = headers;
    h["Content-Type"] = "application/x-www-form-urlencoded";
    return perform("POST", url, h, url_form_encode(form), timeout_ms);
}

}  // namespace ddg
