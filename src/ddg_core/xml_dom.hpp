// SPDX-License-Identifier: AGPL-3.0-only
//
// Minimal read-only DOM built with QXmlStreamReader. Used as the HTML parser
// fallback when the vendored lexbor parser is disabled.

#pragma once

#include <QHash>
#include <QString>

#include <memory>
#include <vector>

namespace ddg::xml {

struct Node {
    QString                            tag;  // empty for text nodes
    QHash<QString, QString>            attrs;
    QString                            text;  // text node contents
    std::vector<std::unique_ptr<Node>> children;

    bool is_text() const { return tag.isEmpty(); }
};

/// Parse simplified HTML/XML. Returns nullptr when the document is malformed.
std::unique_ptr<Node> parse(const QString& html);

/// Attribute lookup; returns an empty string when absent.
QString attr(const Node* node, const QString& name);

/// Concatenated descendant text. Collapses runs of whitespace when `collapse`.
QString text(const Node* node, bool collapse);

/// True when the node's `class` attribute contains `name` as a whole token.
bool has_class(const Node* node, const QString& name);

const Node*              find_first(const Node* node, const QString& tag);
std::vector<const Node*> find_all(const Node* node, const QString& tag);
const Node*              select_one_class(const Node* node, const QString& cls);
std::vector<const Node*> select_class(const Node* node, const QString& cls);

}  // namespace ddg::xml
