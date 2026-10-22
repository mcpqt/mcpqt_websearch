// SPDX-License-Identifier: Apache-2.0

#include "xml_dom.hpp"

#include <QXmlStreamReader>

#include <cctype>

namespace ddg::xml {
namespace {

QString lower(QString s) {
    s = s.toLower();
    return s;
}

bool is_space(QChar ch) { return ch.isSpace(); }

std::string collapse_ws(const QString& in) {
    QString out;
    out.reserve(in.size());
    bool pending_space = false;
    bool started       = false;
    for (QChar ch : in) {
        if (is_space(ch)) {
            pending_space = true;
            continue;
        }
        if (pending_space && started) out += QLatin1Char(' ');
        out += ch;
        pending_space = false;
        started       = true;
    }
    return out.toStdString();
}

void collect_text(const Node* node, QString& out) {
    if (!node) return;
    if (node->is_text()) {
        out += node->text;
        return;
    }
    for (const auto& child : node->children) collect_text(child.get(), out);
}

}  // namespace

std::unique_ptr<Node> parse(const std::string& html) {
    QXmlStreamReader reader(QString::fromStdString(html));
    auto             root = std::make_unique<Node>();
    root->tag             = QStringLiteral("#document");
    Node* current         = root.get();

    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.hasError()) break;

        if (reader.isStartElement()) {
            auto node    = std::make_unique<Node>();
            node->tag    = lower(reader.name().toString());
            for (const auto& a : reader.attributes())
                node->attrs.insert(lower(a.name().toString()), a.value().toString());
            Node* raw    = node.get();
            raw->children.clear();
            raw->attrs.insert(QStringLiteral("__parent__"),
                              QString::number(reinterpret_cast<quintptr>(current)));
            current->children.push_back(std::move(node));
            current = raw;
        } else if (reader.isEndElement()) {
            const quintptr parent = current->attrs.value(QStringLiteral("__parent__")).toULongLong();
            current->attrs.remove(QStringLiteral("__parent__"));
            current = reinterpret_cast<Node*>(parent);
            if (!current) break;
        } else if (reader.isCharacters() || reader.isCDATA()) {
            auto text_node = std::make_unique<Node>();
            text_node->text = reader.text().toString();
            current->children.push_back(std::move(text_node));
        }
    }

    if (reader.hasError() && root->children.empty()) return nullptr;
    return root;
}

std::string attr(const Node* node, std::string_view name) {
    if (!node) return {};
    const auto it = node->attrs.constFind(QString::fromUtf8(name.data(), static_cast<int>(name.size())));
    if (it == node->attrs.cend()) return {};
    return it.value().toStdString();
}

bool has_class(const Node* node, std::string_view name) {
    if (!node) return false;
    const QString classes = node->attrs.value(QStringLiteral("class"));
    const QString target  = QString::fromUtf8(name.data(), static_cast<int>(name.size()));
    for (const QString& token : classes.split(QChar::Space, Qt::SkipEmptyParts)) {
        if (token == target) return true;
    }
    return false;
}

std::string text(const Node* node, bool collapse) {
    if (!node) return {};
    QString raw;
    collect_text(node, raw);
    if (!collapse) return raw.toStdString();
    return collapse_ws(raw);
}

const Node* find_first(const Node* node, std::string_view tag) {
    if (!node) return nullptr;
    const QString target = QString::fromUtf8(tag.data(), static_cast<int>(tag.size()));
    for (const auto& child : node->children) {
        if (child->is_text()) continue;
        if (child->tag == target) return child.get();
        if (const Node* found = find_first(child.get(), tag)) return found;
    }
    return nullptr;
}

std::vector<const Node*> find_all(const Node* node, std::string_view tag) {
    std::vector<const Node*> out;
    if (!node) return out;
    const QString target = QString::fromUtf8(tag.data(), static_cast<int>(tag.size()));
    for (const auto& child : node->children) {
        if (child->is_text()) continue;
        if (child->tag == target) out.push_back(child.get());
        const auto nested = find_all(child.get(), tag);
        out.insert(out.end(), nested.begin(), nested.end());
    }
    return out;
}

const Node* select_one_class(const Node* node, std::string_view cls) {
    if (!node) return nullptr;
    for (const auto& child : node->children) {
        if (child->is_text()) continue;
        if (has_class(child.get(), cls)) return child.get();
        if (const Node* found = select_one_class(child.get(), cls)) return found;
    }
    return nullptr;
}

std::vector<const Node*> select_class(const Node* node, std::string_view cls) {
    std::vector<const Node*> out;
    if (!node) return out;
    for (const auto& child : node->children) {
        if (child->is_text()) continue;
        if (has_class(child.get(), cls)) out.push_back(child.get());
        const auto nested = select_class(child.get(), cls);
        out.insert(out.end(), nested.begin(), nested.end());
    }
    return out;
}

}  // namespace ddg::xml
