// SPDX-License-Identifier: AGPL-3.0-only
//
// Full HTML -> text extraction. Fetched pages are reduced to their main article
// with the Readability port in readability.cpp; the search tool uses
// page_parser_xml.cpp instead.

#pragma once

#include <QString>

namespace ddg::html {

/// mode is "text" (default) or "markdown". `base_url` is the page URL used to
/// resolve relative links while extracting the article. "raw" is handled by
/// the fetcher and bypasses extraction entirely.
QString html_to_text(const QString& html, const QString& mode = "text",
                     const QString& base_url = QString());

}  // namespace ddg::html
