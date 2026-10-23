// SPDX-License-Identifier: Apache-2.0
//
// QXmlStreamReader-backed HTML -> text extraction. Compiled only when
// DDG_USE_LEXBOR is disabled.

#include "html_extract.hpp"

#include "xml_dom.hpp"

#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include <string_view>

namespace ddg::html {
namespace {

bool is_skipped(std::string_view tag) {
    static constexpr std::string_view skipped[] = {
        "script", "style", "noscript", "nav",      "header", "footer",
        "aside",  "form",  "iframe",   "svg",      "canvas", "template",
        "button", "input", "select",   "textarea",
    };
    for (auto s : skipped)
        if (tag == s) return true;
    return false;
}

bool is_block(std::string_view tag) {
    static constexpr std::string_view blocks[] = {
        "p",    "div",  "section", "article", "main", "h1", "h2", "h3",
        "h4",   "h5",   "h6",      "li",      "tr",   "br", "blockquote",
        "pre",  "ul",   "ol",      "table",
    };
    for (auto b : blocks)
        if (tag == b) return true;
    return false;
}

void append_text(const xml::Node* node, QString& out) {
    if (!node) return;
    if (node->is_text()) {
        out += node->text;
        return;
    }
    const std::string tag = node->tag.toStdString();
    if (is_skipped(tag)) return;
    if (tag == "br") {
        out += QLatin1Char('\n');
        return;
    }
    for (const auto& child : node->children) append_text(child.get(), out);
    if (is_block(tag)) out += QLatin1Char('\n');
}

QString collapse_ws(QString in) {
    QStringList lines = in.split(QLatin1Char('\n'));
    QStringList kept;
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
        const QString id   = child->attrs.value(QStringLiteral("id"));
        const QString role = child->attrs.value(QStringLiteral("role"));
        if (id == QStringLiteral("main") || role == QStringLiteral("main") ||
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
    const std::string tag = node->tag.toStdString();
    if (is_skipped(tag)) return;

    if (tag == "br") {
        out += QLatin1Char('\n');
        return;
    }
    if (tag == "a") {
        QString label;
        for (const auto& child : node->children) markdown_walk(child.get(), label, in_pre);
        label = label.simplified();
        const QString href = node->attrs.value(QStringLiteral("href"));
        if (!href.isEmpty() && href.startsWith(QStringLiteral("http")))
            out += QStringLiteral("[%1](%2)").arg(label, href);
        else
            out += label;
        return;
    }
    if (tag == "img") {
        const QString src = node->attrs.value(QStringLiteral("src"));
        const QString alt = node->attrs.value(QStringLiteral("alt"));
        if (!src.isEmpty()) out += QStringLiteral("![%1](%2)").arg(alt, src);
        return;
    }
    if (tag.size() == 2 && tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6') {
        QString label;
        for (const auto& child : node->children) markdown_walk(child.get(), label, in_pre);
        out += QStringLiteral("\n%1 %2\n").arg(QString(tag[1]).repeated(tag[1] - '0'), label.simplified());
        return;
    }
    if (tag == "li") {
        out += QStringLiteral("\n- ");
        for (const auto& child : node->children) markdown_walk(child.get(), out, in_pre);
        return;
    }
    if (is_block(tag)) out += QLatin1Char('\n');

    const bool pre = in_pre || tag == "pre";
    for (const auto& child : node->children) markdown_walk(child.get(), out, pre);

    if (is_block(tag)) out += QLatin1Char('\n');
}

}  // namespace

std::string html_to_text(const std::string& html, const std::string& mode) {
    const auto doc = xml::parse(html);
    if (!doc) return {};

    const xml::Node* start = doc.get();
    if (mode == "main") {
        if (const xml::Node* main = find_main(doc.get())) start = main;
        else if (const xml::Node* article = xml::find_first(doc.get(), "article"))
            start = article;
        else if (const xml::Node* main = xml::find_first(doc.get(), "main")) start = main;
    }

    if (mode == "markdown") {
        QString out;
        markdown_walk(start, out, false);
        QStringList lines = out.split(QLatin1Char('\n'));
        QStringList kept;
        bool previous_blank = false;
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
        static const QRegularExpression numbered_entry(QStringLiteral(R"(^\d+\.)"));
        QStringList spaced;
        for (const QString& line : kept) {
            const bool numbered = numbered_entry.match(line).hasMatch();
            if (numbered && !spaced.isEmpty()) {
                if (!spaced.last().isEmpty()) spaced.push_back(QString());
                spaced.push_back(QString());
            }
            spaced.push_back(line);
        }
        return spaced.join(QLatin1Char('\n')).toStdString();
    }

    QString out;
    append_text(start, out);
    return collapse_ws(out).toStdString();
}

}  // namespace ddg::html
