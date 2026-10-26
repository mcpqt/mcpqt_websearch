// SPDX-License-Identifier: AGPL-3.0-only

#include "xml_dom.hpp"

#include <QRegularExpression>
#include <QStringList>
#include <QXmlStreamReader>

namespace ddg::xml {
namespace {

QString lower(QString s) { return s.toLower(); }

bool is_space(QChar ch) { return ch.isSpace(); }

QString collapse_ws(const QString& in) {
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
    return out;
}

void collect_text(const Node* node, QString& out) {
    if (!node) return;
    if (node->is_text()) {
        out += node->text;
        return;
    }
    for (const auto& child : node->children) collect_text(child.get(), out);
}

QString to_well_formed_xml(QString document) {
    document.replace(QStringLiteral("&nbsp;"), QStringLiteral("&#160;"),
                     Qt::CaseInsensitive);

    static const QStringList kVoidElements = {
        QStringLiteral("link"),   QStringLiteral("meta"), QStringLiteral("input"),
        QStringLiteral("img"),    QStringLiteral("br"),   QStringLiteral("hr"),
        QStringLiteral("source"), QStringLiteral("wbr"),  QStringLiteral("area"),
        QStringLiteral("base"),   QStringLiteral("col"),  QStringLiteral("embed"),
        QStringLiteral("param"),  QStringLiteral("track")};

    static const QRegularExpression tag_re(
        QStringLiteral(R"(<!--.*?-->|<(?:[^>"']|"[^"]*"|'[^']*')*>)"),
        QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression name_re(
        QStringLiteral(R"(^<\s*([A-Za-z][A-Za-z0-9:-]*))"));
    static const QRegularExpression attr_re(
        QStringLiteral(
            R"(([A-Za-z_:][A-Za-z0-9_:.-]*)(?:\s*=\s*("[^"]*"|'[^']*'|[^\s"'>]+))?)"));

    QString out;
    out.reserve(document.size());
    qsizetype last = 0;
    auto      it   = tag_re.globalMatch(document);
    while (it.hasNext()) {
        const auto match = it.next();
        out += document.mid(last, match.capturedStart() - last);

        const QString tag = match.captured();
        if (tag.startsWith(QStringLiteral("</")) ||
            tag.startsWith(QStringLiteral("<!DOCTYPE")) ||
            tag.startsWith(QStringLiteral("<!--")) ||
            tag.startsWith(QStringLiteral("<?"))) {
            out += tag;
            last = match.capturedEnd();
            continue;
        }

        const auto    name_match = name_re.match(tag);
        const QString name       = name_match.captured(1);
        if (name.isEmpty()) {
            out += tag;
            last = match.capturedEnd();
            continue;
        }

        QString attrs      = tag.mid(name_match.capturedLength());
        bool    self_close = attrs.trimmed().endsWith(QLatin1Char('/'));
        if (self_close) attrs = attrs.trimmed().chopped(1);

        QString rebuilt = QLatin1Char('<') + name;
        auto    attr_it = attr_re.globalMatch(attrs);
        while (attr_it.hasNext()) {
            const auto    attr       = attr_it.next();
            const QString attr_name  = attr.captured(1);
            QString       attr_value = attr.captured(2);
            if (attr_value.isEmpty()) {
                attr_value = QLatin1Char('"') + attr_name + QLatin1Char('"');
            } else if (!attr_value.startsWith(QLatin1Char('"')) &&
                       !attr_value.startsWith(QLatin1Char('\''))) {
                attr_value = QLatin1Char('"') + attr_value + QLatin1Char('"');
            }
            rebuilt += QLatin1Char(' ') + attr_name + QLatin1Char('=') + attr_value;
        }

        const bool is_void = kVoidElements.contains(name, Qt::CaseInsensitive);
        rebuilt += (self_close || is_void) ? QStringLiteral("/>") : QStringLiteral(">");
        out += rebuilt;
        last = match.capturedEnd();
    }
    out += document.mid(last);
    return out;
}

}  // namespace

std::unique_ptr<Node> parse(const QString& html) {
    const QString     document = to_well_formed_xml(html);
    QXmlStreamReader reader(document);
    auto             root = std::make_unique<Node>();
    root->tag             = QStringLiteral("#document");
    Node* current         = root.get();

    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.hasError()) break;

        if (reader.isStartElement()) {
            auto node  = std::make_unique<Node>();
            node->tag  = lower(reader.name().toString());
            for (const auto& a : reader.attributes())
                node->attrs.insert(lower(a.name().toString()), a.value().toString());
            Node* raw = node.get();
            raw->children.clear();
            raw->attrs.insert(QStringLiteral("__parent__"),
                              QString::number(reinterpret_cast<quintptr>(current)));
            current->children.push_back(std::move(node));
            current = raw;
        } else if (reader.isEndElement()) {
            const quintptr parent =
                current->attrs.value(QStringLiteral("__parent__")).toULongLong();
            current->attrs.remove(QStringLiteral("__parent__"));
            current = reinterpret_cast<Node*>(parent);
            if (!current) break;
        } else if (reader.isCharacters() || reader.isCDATA()) {
            auto text_node   = std::make_unique<Node>();
            text_node->text  = reader.text().toString();
            current->children.push_back(std::move(text_node));
        }
    }

    if (reader.hasError() && root->children.empty()) return nullptr;
    return root;
}

QString attr(const Node* node, const QString& name) {
    if (!node) return QString();
    const auto it = node->attrs.constFind(name);
    if (it == node->attrs.cend()) return QString();
    return it.value();
}

bool has_class(const Node* node, const QString& name) {
    if (!node) return false;
    const QString classes = node->attrs.value(QStringLiteral("class"));
    for (const QString& token : classes.split(QChar::Space, Qt::SkipEmptyParts)) {
        if (token == name) return true;
    }
    return false;
}

QString text(const Node* node, bool collapse) {
    if (!node) return QString();
    QString raw;
    collect_text(node, raw);
    if (!collapse) return raw;
    return collapse_ws(raw);
}

const Node* find_first(const Node* node, const QString& tag) {
    if (!node) return nullptr;
    for (const auto& child : node->children) {
        if (child->is_text()) continue;
        if (child->tag == tag) return child.get();
        if (const Node* found = find_first(child.get(), tag)) return found;
    }
    return nullptr;
}

std::vector<const Node*> find_all(const Node* node, const QString& tag) {
    std::vector<const Node*> out;
    if (!node) return out;
    for (const auto& child : node->children) {
        if (child->is_text()) continue;
        if (child->tag == tag) out.push_back(child.get());
        const auto nested = find_all(child.get(), tag);
        out.insert(out.end(), nested.begin(), nested.end());
    }
    return out;
}

const Node* select_one_class(const Node* node, const QString& cls) {
    if (!node) return nullptr;
    for (const auto& child : node->children) {
        if (child->is_text()) continue;
        if (has_class(child.get(), cls)) return child.get();
        if (const Node* found = select_one_class(child.get(), cls)) return found;
    }
    return nullptr;
}

std::vector<const Node*> select_class(const Node* node, const QString& cls) {
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
