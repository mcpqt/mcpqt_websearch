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
        QStringLiteral("script"), QStringLiteral("style"),  QStringLiteral("noscript"),
        QStringLiteral("nav"),    QStringLiteral("header"), QStringLiteral("footer"),
        QStringLiteral("aside"),  QStringLiteral("form"),   QStringLiteral("iframe"),
        QStringLiteral("svg"),    QStringLiteral("canvas"), QStringLiteral("template"),
        QStringLiteral("button"), QStringLiteral("input"),  QStringLiteral("select"),
        QStringLiteral("textarea"), QStringLiteral("img"),
    };
    return skipped.contains(tag);
}

bool is_block(const QString& tag) {
    static const QStringList blocks = {
        QStringLiteral("p"),    QStringLiteral("div"),  QStringLiteral("section"),
        QStringLiteral("article"), QStringLiteral("main"), QStringLiteral("h1"),
        QStringLiteral("h2"),   QStringLiteral("h3"),   QStringLiteral("h4"),
        QStringLiteral("h5"),   QStringLiteral("h6"),   QStringLiteral("li"),
        QStringLiteral("tr"),   QStringLiteral("br"),   QStringLiteral("blockquote"),
        QStringLiteral("pre"),  QStringLiteral("ul"),   QStringLiteral("ol"),
        QStringLiteral("table"),
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
    if (tag == QStringLiteral("br")) {
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
        const QString id   = child->attrs.value(QStringLiteral("id"));
        const QString role = child->attrs.value(QStringLiteral("role"));
        if (id == QStringLiteral("main") || role == QStringLiteral("main") ||
            xml::has_class(child.get(), QStringLiteral("main"))) {
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

    if (tag == QStringLiteral("br")) {
        out += QLatin1Char('\n');
        return;
    }
    if (tag == QStringLiteral("a")) {
        QString label;
        for (const auto& child : node->children) markdown_walk(child.get(), label, in_pre);
        label              = label.simplified();
        const QString href = node->attrs.value(QStringLiteral("href"));
        if (!href.isEmpty() && href.startsWith(QStringLiteral("http")))
            out += QStringLiteral("[%1](%2)").arg(label, href);
        else
            out += label;
        return;
    }
    if (tag.size() == 2 && tag.at(0) == QLatin1Char('h') &&
        tag.at(1) >= QLatin1Char('1') && tag.at(1) <= QLatin1Char('6')) {
        QString label;
        for (const auto& child : node->children) markdown_walk(child.get(), label, in_pre);
        const QChar level = tag.at(1);
        out += QStringLiteral("\n%1 %2\n")
                   .arg(QString(level.digitValue(), QLatin1Char('#')), label.simplified());
        return;
    }
    if (tag == QStringLiteral("li")) {
        out += QStringLiteral("\n- ");
        for (const auto& child : node->children) markdown_walk(child.get(), out, in_pre);
        return;
    }
    if (is_block(tag)) out += QLatin1Char('\n');

    const bool pre = in_pre || tag == QStringLiteral("pre");
    for (const auto& child : node->children) markdown_walk(child.get(), out, pre);

    if (is_block(tag)) out += QLatin1Char('\n');
}

}  // namespace

QString html_to_text(const QString& html, const QString& mode) {
    const auto doc = xml::parse(html);
    if (!doc) return QString();

    const xml::Node* start = doc.get();
    if (mode == QStringLiteral("main")) {
        if (const xml::Node* main = find_main(doc.get())) start = main;
        else if (const xml::Node* article =
                     xml::find_first(doc.get(), QStringLiteral("article")))
            start = article;
        else if (const xml::Node* main =
                     xml::find_first(doc.get(), QStringLiteral("main")))
            start = main;
    }

    if (mode == QStringLiteral("markdown")) {
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
        static const QRegularExpression numbered_entry(QStringLiteral(R"(^\d+\.)"));
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
