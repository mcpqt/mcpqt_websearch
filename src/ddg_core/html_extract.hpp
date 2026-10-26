// SPDX-License-Identifier: AGPL-3.0-only
//
// Full HTML -> text extraction (server.py _html_to_text). Always Lexbor-backed;
// the search tool uses page_parser_xml.cpp instead.

#pragma once

#include <QString>

namespace ddg::html {

/// mode is "text" (default), "main" or "markdown".
QString html_to_text(const QString& html, const QString& mode = QStringLiteral("text"));

}  // namespace ddg::html
