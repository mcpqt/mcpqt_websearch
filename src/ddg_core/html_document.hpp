// SPDX-License-Identifier: AGPL-3.0-only
//
// RAII owner for a parsed lexbor HTML document. Borrowed DOM node pointers
// (lxb_dom_node_t*) are only valid while the owning Document is alive.

#pragma once

#include <lexbor/html/html.h>

#include <QString>

namespace ddg::html {

class Document {
public:
    Document() = default;
    explicit Document(lxb_html_document_t* doc) : doc_(doc) {}

    Document(const Document&)            = delete;
    Document& operator=(const Document&) = delete;

    Document(Document&& other) noexcept : doc_(other.doc_) { other.doc_ = nullptr; }
    Document& operator=(Document&& other) noexcept {
        if (this != &other) {
            destroy();
            doc_       = other.doc_;
            other.doc_ = nullptr;
        }
        return *this;
    }

    ~Document() { destroy(); }

    /// Parse an HTML string. On failure the returned Document is invalid().
    static Document parse(const QString& html);

    bool                valid() const { return doc_ != nullptr; }
    explicit operator bool() const { return doc_ != nullptr; }

    lxb_html_document_t* raw() const { return doc_; }

    /// The document node (root of the tree; children are <html> etc.).
    lxb_dom_node_t* root() const;

private:
    void destroy();

    lxb_html_document_t* doc_ = nullptr;
};

}  // namespace ddg::html
