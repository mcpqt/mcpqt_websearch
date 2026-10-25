// SPDX-License-Identifier: AGPL-3.0-only

#include "html_document.hpp"

namespace ddg::html {

Document Document::parse(const std::string& html) {
    lxb_html_document_t* doc = lxb_html_document_create();
    if (!doc) return Document{};

    const auto* data = reinterpret_cast<const lxb_char_t*>(html.data());
    if (lxb_html_document_parse(doc, data, html.size()) != LXB_STATUS_OK) {
        lxb_html_document_destroy(doc);
        return Document{};
    }
    return Document(doc);
}

lxb_dom_node_t* Document::root() const {
    if (!doc_) return nullptr;
    return lxb_dom_interface_node(doc_);
}

void Document::destroy() {
    if (doc_) {
        lxb_html_document_destroy(doc_);
        doc_ = nullptr;
    }
}

}  // namespace ddg::html
