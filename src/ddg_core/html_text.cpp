// SPDX-License-Identifier: AGPL-3.0-only

#include "html_text.hpp"

#include "html_query.hpp"
#include "utils.hpp"

namespace ddg::html {
namespace {

QString node_text(lxb_dom_node_t* node) {
    auto* cd = lxb_dom_interface_character_data(node);
    if (!cd || !cd->data.data) return QString();
    return QString::fromUtf8(reinterpret_cast<const char*>(cd->data.data),
                             static_cast<int>(cd->data.length));
}

void walk(lxb_dom_node_t* node, const QString& sep, bool strip, QString& out, bool& first) {
    if (is_text(node)) {
        QString s = node_text(node);
        if (strip) s = s.trimmed();
        if (strip && s.isEmpty()) return;
        if (!first) out += sep;
        out += s;
        first = false;
        return;
    }
    for (lxb_dom_node_t* c = node->first_child; c; c = c->next)
        walk(c, sep, strip, out, first);
}

}  // namespace

QString get_text(lxb_dom_node_t* node, const QString& separator, bool strip) {
    QString out;
    bool    first = true;
    if (node) walk(node, separator, strip, out, first);
    return out;
}

void decompose(lxb_dom_node_t* node) {
    if (node) lxb_dom_node_remove(node);
}

}  // namespace ddg::html
