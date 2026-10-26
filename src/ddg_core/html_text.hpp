// SPDX-License-Identifier: AGPL-3.0-only
//
// Text extraction / node removal over a lexbor DOM (BeautifulSoup get_text /
// decompose equivalents).

#pragma once

#include <lexbor/dom/dom.h>

#include <QString>

namespace ddg::html {

/// Concatenate the text of all descendant text nodes. Mirrors BeautifulSoup
/// `get_text(separator, strip)`: when `strip`, each string is trimmed and
/// empties dropped before joining with `separator`.
QString get_text(lxb_dom_node_t* node, const QString& separator = QString(),
                 bool strip = false);

/// Detach a node (and its subtree) from the tree. The storage is owned by the
/// Document and released when it is destroyed. (BeautifulSoup `.decompose`.)
void decompose(lxb_dom_node_t* node);

}  // namespace ddg::html
