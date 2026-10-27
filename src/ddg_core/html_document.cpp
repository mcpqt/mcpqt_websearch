// SPDX-License-Identifier: AGPL-3.0-only

#include "html_document.hpp"

#include <QByteArray>

namespace ddg::html {

Document Document::parse(const QString& html) {
    lxb_html_document_t* doc = lxb_html_document_create();
    if (!doc) return Document{};

    const QByteArray utf8 = html.toUtf8();
    const auto*      data = reinterpret_cast<const lxb_char_t*>(utf8.constData());
    if (lxb_html_document_parse(doc, data, static_cast<size_t>(utf8.size())) !=
        LXB_STATUS_OK) {
        lxb_html_document_destroy(doc);
        return Document{};
    }
    return Document(doc);
}

lxb_dom_node_t* Document::root() const {
    if (!m_doc) return nullptr;
    return lxb_dom_interface_node(m_doc);
}

void Document::destroy() {
    if (m_doc) {
        lxb_html_document_destroy(m_doc);
        m_doc = nullptr;
    }
}

}  // namespace ddg::html
