// SPDX-License-Identifier: AGPL-3.0-only
//
// QXmlStreamReader-backed HTML -> text extraction. Not used by the build:
// fetched pages are always extracted with the Lexbor parser. Retained for
// reference.

#include "html_extract.hpp"

#include "xml_dom.hpp"

#include <QRegularExpression>
#include <QString>
#include <QStringList>

namespace ddg::html {
namespace {

bool is_skipped(const QString& tag) {
    static const QStringList skipped = {
        "script", "style",  "noscript",
        "nav",    "header", "footer",
        "aside",  "form",   "iframe",
        "svg",    "canvas", "template",
        "button", "input",  "select",
        "textarea", "img",
    };
    return skipped.contains(tag);
}

bool is_block(const QString& tag) {
    static const QStringList blocks = {
        "p",    "div",  "section",
        "article", "main", "h1",
        "h2",   "h3",   "h4",
        "h5",   "h6",   "li",
        "tr",   "br",   "blockquote",
        "pre",  "ul",   "ol",
        "table",
    };
    return blocks.contains(tag);
}

void append_text(const xml::Node* node, QString& out) {
    if (!node) return;
    if (node->is_text()) {
        out += node->text;
        return;
    }
    const QString& tag = node->tag;
    if (is_skipped(tag)) return;
    if (tag == "br") {
        out += QLatin1Char('\n');
        return;
    }
    for (const auto& child : node->children) append_text(child.get(), out);
    if (is_block(tag)) out += QLatin1Char('\n');
}

QString collapse_ws(QString in) {
    QStringList      lines = in.split(QLatin1Char('\n'));
    QStringList      kept;
    for (QString& line : lines) {
        line = line.simplified();
        if (!line.isEmpty()) kept.push_back(line);
    }
    return kept.join(QLatin1Char('\n'));
}

const xml::Node* find_main(const xml::Node* node) {
    if (!node) return nullptr;
    for (const auto& child : node->children) {
        if (child->is_text()) continue;
        const QString id   = child->attrs.value("id");
        const QString role = child->attrs.value("role");
        if (id == "main" || role == "main" ||
            xml::has_class(child.get(), "main")) {
            return child.get();
        }
        if (const xml::Node* found = find_main(child.get())) return found;
    }
    return nullptr;
}

void markdown_walk(const xml::Node* node, QString& out, bool in_pre) {
    if (!node) return;
    if (node->is_text()) {
        out += in_pre ? node->text : node->text.simplified();
        if (!in_pre && !node->text.isEmpty()) out += QLatin1Char(' ');
        return;
    }
    const QString& tag = node->tag;
    if (is_skipped(tag)) return;

    if (tag == "br") {
        out += QLatin1Char('\n');
        return;
    }
    if (tag == "a") {
        QString label;
        for (const auto& child : node->children) markdown_walk(child.get(), label, in_pre);
        label              = label.simplified();
        const QString href = node->attrs.value("href");
        if (!href.isEmpty() && href.startsWith("http"))
            out += QString("[%1](%2)").arg(label, href);
        else
            out += label;
        return;
    }
    if (tag.size() == 2 && tag.at(0) == QLatin1Char('h') &&
        tag.at(1) >= QLatin1Char('1') && tag.at(1) <= QLatin1Char('6')) {
        QString label;
        for (const auto& child : node->children) markdown_walk(child.get(), label, in_pre);
        const QChar level = tag.at(1);
        out += QString("\n%1 %2\n")
                   .arg(QString(level.digitValue(), QLatin1Char('#')), label.simplified());
        return;
    }
    if (tag == "li") {
        out += "\n- ";
        for (const auto& child : node->children) markdown_walk(child.get(), out, in_pre);
        return;
    }
    if (is_block(tag)) out += QLatin1Char('\n');

    const bool pre = in_pre || tag == "pre";
    for (const auto& child : node->children) markdown_walk(child.get(), out, pre);

    if (is_block(tag)) out += QLatin1Char('\n');
}

}  // namespace

QString html_to_text(const QString& html, const QString& mode) {
    const auto doc = xml::parse(html);
    if (!doc) return QString();

    const xml::Node* start = doc.get();
    if (mode == "main") {
        if (const xml::Node* main = find_main(doc.get())) start = main;
        else if (const xml::Node* article =
                     xml::find_first(doc.get(), "article"))
            start = article;
        else if (const xml::Node* main =
                     xml::find_first(doc.get(), "main"))
            start = main;
    }

    if (mode == "markdown") {
        QString out;
        markdown_walk(start, out, false);
        QStringList lines = out.split(QLatin1Char('\n'));
        QStringList kept;
        bool        previous_blank = false;
        for (QString& line : lines) {
            line = line.simplified();
            if (line.isEmpty()) {
                if (!kept.isEmpty() && !previous_blank) {
                    kept.push_back(QString());
                    previous_blank = true;
                }
            } else {
                kept.push_back(line);
                previous_blank = false;
            }
        }
        while (!kept.isEmpty() && kept.last().isEmpty()) kept.removeLast();
        static const QRegularExpression numbered_entry(R"(^\d+\.)");
        QStringList                     spaced;
        for (const QString& line : kept) {
            const bool numbered = numbered_entry.match(line).hasMatch();
            if (numbered && !spaced.isEmpty()) {
                if (!spaced.last().isEmpty()) spaced.push_back(QString());
                spaced.push_back(QString());
            }
            spaced.push_back(line);
        }
        return spaced.join(QLatin1Char('\n'));
    }

    QString out;
    append_text(start, out);
    return collapse_ws(out);
}

}  // namespace ddg::html
