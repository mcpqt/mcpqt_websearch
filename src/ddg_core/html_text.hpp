// SPDX-License-Identifier: Apache-2.0
//
// Text extraction / node removal over a lexbor DOM (BeautifulSoup get_text /
// decompose equivalents).

#pragma once

#include <lexbor/dom/dom.h>

#include <string>

namespace ddg::html {

/// Concatenate the text of all descendant text nodes. Mirrors BeautifulSoup
/// `get_text(separator, strip)`: when `strip`, each string is trimmed and
/// empties dropped before joining with `separator`.
std::string get_text(lxb_dom_node_t* node, const std::string& separator = "", bool strip = false);

/// Detach a node (and its subtree) from the tree. The storage is owned by the
/// Document and released when it is destroyed. (BeautifulSoup `.decompose`.)
void decompose(lxb_dom_node_t* node);

}  // namespace ddg::html
