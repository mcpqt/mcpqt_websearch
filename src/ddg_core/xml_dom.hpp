// SPDX-License-Identifier: AGPL-3.0-only
//
// Minimal read-only DOM built with QXmlStreamReader. Used as the HTML parser
// fallback when the vendored lexbor parser is disabled.

#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <QHash>
#include <QString>

namespace ddg::xml {

struct Node {
    QString                              tag;  // empty for text nodes
    QHash<QString, QString>              attrs;
    QString                              text;  // text node contents
    std::vector<std::unique_ptr<Node>>   children;

    bool is_text() const { return tag.isEmpty(); }
};

/// Parse simplified HTML/XML. Returns nullptr when the document is malformed.
std::unique_ptr<Node> parse(const std::string& html);

/// Attribute lookup; returns an empty string when absent.
std::string attr(const Node* node, std::string_view name);

/// Concatenated descendant text. Collapses runs of whitespace when `collapse`.
std::string text(const Node* node, bool collapse);

/// True when the node's `class` attribute contains `name` as a whole token.
bool has_class(const Node* node, std::string_view name);

const Node* find_first(const Node* node, std::string_view tag);
std::vector<const Node*> find_all(const Node* node, std::string_view tag);
const Node* select_one_class(const Node* node, std::string_view cls);
std::vector<const Node*> select_class(const Node* node, std::string_view cls);

}  // namespace ddg::xml
