// SPDX-License-Identifier: Apache-2.0
//
// TLS verification settings (server.py DDG_SSL_VERIFY / DDG_CA_CERTS).

#pragma once

#include <string>

namespace ddg {

struct TlsVerify {
    bool        enabled = true;
    std::string ca_bundle;  // when non-empty (and enabled), trust this PEM file
};

}  // namespace ddg
