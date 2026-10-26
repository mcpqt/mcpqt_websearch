// SPDX-License-Identifier: AGPL-3.0-only
//
// Transport adapter that routes ddg::HttpRequest through the webio plugin.

#pragma once

#include "http_transport.hpp"

#include <QString>
#include <QVariant>
#include <QVariantList>

#include <functional>

namespace ddg {

/// Shape of mcp::HostInterface::callFunc.
using HostCallFunc = std::function<QVariant(const QString&, const QVariantList&)>;

/// Build a Transport that invokes webio's httpGet/httpPost through the host.
Transport make_webio_transport(HostCallFunc call_func);

}  // namespace ddg
