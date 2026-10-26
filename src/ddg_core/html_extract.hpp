// SPDX-License-Identifier: AGPL-3.0-only
//
// Full HTML -> text extraction (server.py _html_to_text).

#pragma once

#include <QString>

namespace ddg::html {

/// mode is "text" (default), "main" or "markdown".
QString html_to_text(const QString& html, const QString& mode = QStringLiteral("text"));

}  // namespace ddg::html
