// SPDX-License-Identifier: AGPL-3.0-only
//
// DOM query helpers backed by lexbor's CSS selector engine (BeautifulSoup's
// select / find / find_all equivalents). Returned lxb_dom_node_t* pointers are
// borrowed: they stay valid until the owning Document is destroyed.

#pragma once

#include <lexbor/dom/dom.h>

#include <QString>

#include <vector>

namespace ddg::html {

/// All descendants (document order) matching a CSS selector.
std::vector<lxb_dom_node_t*> select(lxb_dom_node_t* root, const QString& selector);
lxb_dom_node_t*              select_one(lxb_dom_node_t* root, const QString& selector);

/// First descendant element with the given tag name (BeautifulSoup `.find`).
lxb_dom_node_t* find(lxb_dom_node_t* node, const QString& name);

/// All descendant elements with the given tag name. `recursive=false` keeps
/// only direct children. (BeautifulSoup `.find_all`.)
std::vector<lxb_dom_node_t*> find_all(lxb_dom_node_t* node, const QString& name,
                                      bool recursive = true);

/// Lowercase local name of an element node, or "" for non-elements.
QString tag_name(lxb_dom_node_t* node);

bool is_element(lxb_dom_node_t* node);
bool is_text(lxb_dom_node_t* node);

/// Entity-decoded value of an attribute, or "" when absent/non-element.
QString attr(lxb_dom_node_t* node, const QString& name);
bool    has_attr(lxb_dom_node_t* node, const QString& name);

}  // namespace ddg::html
