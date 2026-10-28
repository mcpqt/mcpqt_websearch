// SPDX-License-Identifier: AGPL-3.0-only

#include "html_extract.hpp"

#include "error.hpp"
#include "html_document.hpp"
#include "html_query.hpp"
#include "html_text.hpp"
#include "readability.hpp"
#include "utils.hpp"

#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include <functional>
#include <vector>

namespace ddg::html {
namespace {

const QStringList kBlockTags = {
    "address", "article", "aside",
    "blockquote", "dd", "details",
    "div", "dl", "dt",
    "fieldset", "figcaption", "figure",
    "footer", "form", "h1",
    "h2", "h3", "h4",
    "h5", "h6", "header",
    "hr", "li", "main",
    "nav", "ol", "p",
    "pre", "section", "summary",
    "table", "tbody", "td",
    "tfoot", "th", "thead",
    "tr", "ul"};

bool is_block_tag(const QString& tag) { return kBlockTags.contains(tag); }

const QStringList kTextChromeTags = {"script", "style",
                                     "nav", "header",
                                     "footer"};

void strip_chrome(lxb_dom_node_t* root, const QStringList& tags) {
    std::vector<lxb_dom_node_t*> to_remove;
    for (const QString& t : tags) {
        auto found = find_all(root, t, true);
        to_remove.insert(to_remove.end(), found.begin(), found.end());
    }
    for (auto* node : to_remove) decompose(node);
}

QString safe_markdown_href(const QString& href) {
    QString cleaned;
    for (QChar ch : href) {
        if (ch >= QLatin1Char(' ') && ch != QLatin1Char('\r') && ch != QLatin1Char('\n'))
            cleaned += ch;
    }
    cleaned = cleaned.trimmed();  // `.strip()`
    if (cleaned.isEmpty()) return QString();
    for (QChar ch : cleaned) {
        if (ch.isSpace()) return QString();
    }
    const qsizetype colon = cleaned.indexOf("://");
    if (colon == -1) return QString();
    const QString scheme = cleaned.left(colon).toLower();
    if (scheme != "http" && scheme != "https")
        return QString();
    // A netloc is required (chars between "://" and the next '/').
    if (colon + 3 >= cleaned.size()) return QString();
    if (cleaned.at(colon + 3) == QLatin1Char('/')) return QString();

    QString out;
    for (QChar ch : cleaned) {
        if (ch == QLatin1Char(')'))
            out += "%29";
        else
            out += ch;
    }
    return out;
}

QString safe_markdown_label(const QString& label) {
    QString out;
    for (QChar ch : label) {
        if (ch == QLatin1Char('\r') || ch == QLatin1Char('\n') ||
            ch == QLatin1Char('[') || ch == QLatin1Char(']'))
            continue;
        out += ch;
    }
    return out.trimmed();
}

QString inline_markdown(lxb_dom_node_t* el);

QString inline_markdown(lxb_dom_node_t* el) {
    if (is_text(el)) {
        return get_text(el, QString(), false)
            .replace(QRegularExpression("\\s+"), " ");
    }
    const QString name = tag_name(el);
    if (name == "br") return "\n";
    if (name == "a") {
        const QString href  = safe_markdown_href(attr(el, "href"));
        const QString label = safe_markdown_label(get_text(el, " ", true));
        if (!href.isEmpty() && !label.isEmpty())
            return "[" + label + "](" + href +
                   ")";
        return label;
    }
    if (name == "code")
        return "`" + get_text(el, QString(), false) + "`";
    if (name == "strong" || name == "b") {
        const QString inner = get_text(el, " ", true);
        return inner.isEmpty() ? QString() : "**" + inner + "**";
    }
    if (name == "em" || name == "i") {
        const QString inner = get_text(el, " ", true);
        return inner.isEmpty() ? QString() : "*" + inner + "*";
    }
    QString out;
    for (lxb_dom_node_t* child = el->first_child; child; child = child->next) {
        out += inline_markdown(child);
    }
    return out;
}

void render_markdown(lxb_dom_node_t* el, QStringList& parts) {
    if (is_text(el)) {
        const QString text = get_text(el, QString(), false).trimmed();
        if (!text.isEmpty()) parts.push_back(text);
        return;
    }
    const QString name = tag_name(el);
    if (name.size() == 2 && name.at(0) == QLatin1Char('h') &&
        name.at(1) >= QLatin1Char('1') && name.at(1) <= QLatin1Char('6')) {
        const QChar level = name.at(1);
        parts.push_back(QString(level.digitValue(), QLatin1Char('#')) + QLatin1Char(' ') +
                        get_text(el, " ", true));
        parts.push_back(QString());
    } else if (name == "p") {
        const QString text = inline_markdown(el).trimmed();
        if (!text.isEmpty()) {
            parts.push_back(text);
            parts.push_back(QString());
        }
    } else if (name == "pre") {
        QString code = get_text(el, QString(), false);
        if (!code.isEmpty() && code.endsWith(QLatin1Char('\n'))) code.chop(1);
        parts.push_back("```");
        parts.push_back(code);
        parts.push_back("```");
        parts.push_back(QString());
    } else if (name == "ul" || name == "ol") {
        int i = 1;
        for (auto* li : find_all(el, "li", false)) {
            const QString bullet = (name == "ol")
                                       ? QString::number(i) + QLatin1Char('.')
                                       : "-";
            parts.push_back(bullet + QLatin1Char(' ') +
                            get_text(li, " ", true));
            ++i;
        }
        parts.push_back(QString());
    } else if (name == "blockquote") {
        const QString quote = get_text(el, " ", true);
        if (!quote.isEmpty()) {
            parts.push_back("> " + quote);
            parts.push_back(QString());
        }
    } else if (name == "hr") {
        parts.push_back("---");
        parts.push_back(QString());
    } else {
        bool previous_was_element = false;
        for (lxb_dom_node_t* child = el->first_child; child; child = child->next) {
            if (is_element(child)) {
                if (previous_was_element) parts.push_back(QString());
                render_markdown(child, parts);
                previous_was_element = true;
            } else if (is_text(child)) {
                render_markdown(child, parts);
                if (!get_text(child, QString(), false).trimmed().isEmpty())
                    previous_was_element = false;
            }
        }
    }
}

QString render_plain_text(lxb_dom_node_t* root) {
    QString out;

    auto ensure_newline = [&out]() {
        if (!out.isEmpty() && !out.endsWith(QLatin1Char('\n'))) out += QLatin1Char('\n');
    };
    auto ensure_space = [&out]() {
        if (!out.isEmpty() && !out.endsWith(QLatin1Char(' ')) &&
            !out.endsWith(QLatin1Char('\n')))
            out += QLatin1Char(' ');
    };

    std::function<void(lxb_dom_node_t*)> walk = [&](lxb_dom_node_t* node) {
        bool previous_was_element = false;
        for (lxb_dom_node_t* child = node->first_child; child; child = child->next) {
            if (is_text(child)) {
                QString text = get_text(child, QString(), false);
                text.replace(QRegularExpression("[ \t\r\n]+"),
                             " ");
                if (!text.isEmpty()) {
                    if (!text.trimmed().isEmpty()) previous_was_element = false;
                    out += text;
                }
                continue;
            }
            if (!is_element(child)) continue;

            const QString tag = tag_name(child);
            const bool    block = is_block_tag(tag);
            if (tag == "br") {
                ensure_newline();
                previous_was_element = false;
                continue;
            }
            if (block) {
                ensure_newline();
            } else if (previous_was_element) {
                ensure_space();
            }

            walk(child);

            if (block) ensure_newline();
            previous_was_element = !block;
        }
    };

    if (root) walk(root);

    out.replace(QRegularExpression("[ \t]+"), " ");
    out.replace(QRegularExpression(" *\n *"), "\n");
    out.replace(QRegularExpression("\n{3,}"), "\n\n");
    return out.trimmed();
}

QString html_to_markdown(lxb_dom_node_t* root) {
    QStringList parts;
    render_markdown(root, parts);
    QString text = parts.join(QLatin1Char('\n'));
    text.replace(QRegularExpression("\\n{3,}"), "\n\n");
    return text.trimmed();
}

}  // namespace

QString html_to_text(const QString& html, const QString& mode, const QString& base_url) {
    if (mode != "text" && mode != "markdown") {
        throw InvalidArgument(
            QString("Unknown parse mode '%1'. Supported: text, markdown")
                .arg(mode));
    }

    QString extracted;

    Article article = extract_article(html, base_url);
    if (article.ok) {
        extracted = mode == "markdown" ? html_to_markdown(article.content)
                                       : render_plain_text(article.content);
    } else {
        // Readability needs a body with readable content. For tiny/structural
        // documents fall back to rendering the document body directly.
        Document doc = Document::parse(html);
        if (doc.valid()) {
            lxb_dom_node_t* root = doc.root();
            if (mode == "text") {
                strip_chrome(root, kTextChromeTags);
                extracted = render_plain_text(root);
            } else {
                extracted = html_to_markdown(root);
            }
        }
    }

    if (!extracted.isEmpty()) return extracted;

    return "Content extraction failed; returning raw page contents.\n\n" +
           html;
}

}  // namespace ddg::html
