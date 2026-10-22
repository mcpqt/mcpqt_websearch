// SPDX-License-Identifier: AGPL-3.0-only
// SPDX-License-Identifier: Apache-2.0

#include "html_text.hpp"

#include "html_query.hpp"
#include "utils.hpp"

namespace ddg::html {
namespace {

std::string node_text(lxb_dom_node_t* node) {
    auto* cd = lxb_dom_interface_character_data(node);
    if (!cd || !cd->data.data) return {};
    return std::string(reinterpret_cast<const char*>(cd->data.data), cd->data.length);
}

void walk(lxb_dom_node_t* node, const std::string& sep, bool strip, std::string& out, bool& first) {
    if (is_text(node)) {
        std::string s = node_text(node);
        if (strip) s = trim(s);
        if (strip && s.empty()) return;
        if (!first) out += sep;
        out += s;
        first = false;
        return;
    }
    for (lxb_dom_node_t* c = node->first_child; c; c = c->next) walk(c, sep, strip, out, first);
}

}  // namespace

std::string get_text(lxb_dom_node_t* node, const std::string& separator, bool strip) {
    std::string out;
    bool        first = true;
    if (node) walk(node, separator, strip, out, first);
    return out;
}

void decompose(lxb_dom_node_t* node) {
    if (node) lxb_dom_node_remove(node);
}

}  // namespace ddg::html
