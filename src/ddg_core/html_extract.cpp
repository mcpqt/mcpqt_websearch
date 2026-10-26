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

#include <vector>

namespace ddg::html {
namespace {

const char* const kMainSelectors[] = {
    "article", "main", "[role='main']", "#content", "#main", "#main-content",
    ".post-content", ".entry-content", ".article-body", ".article-content",
    ".post-body", ".markdown-body",
};

const QStringList kTextChromeTags = {QStringLiteral("script"), QStringLiteral("style"),
                                     QStringLiteral("nav"), QStringLiteral("header"),
                                     QStringLiteral("footer")};
const QStringList kMainChromeTags = {QStringLiteral("script"), QStringLiteral("style"),
                                     QStringLiteral("nav"), QStringLiteral("header"),
                                     QStringLiteral("footer"), QStringLiteral("aside"),
                                     QStringLiteral("form"), QStringLiteral("noscript")};

void strip_chrome(lxb_dom_node_t* root, const QStringList& tags) {
    std::vector<lxb_dom_node_t*> to_remove;
    for (const QString& t : tags) {
        auto found = find_all(root, t, true);
        to_remove.insert(to_remove.end(), found.begin(), found.end());
    }
    for (auto* node : to_remove) decompose(node);
}

lxb_dom_node_t* select_main_root(lxb_dom_node_t* root) {
    for (const char* selector : kMainSelectors) {
        auto* found = select_one(root, QString::fromLatin1(selector));
        if (found && get_text(found, QStringLiteral(" "), true).size() >= 40) return found;
    }
    if (auto* body = find(root, QStringLiteral("body"))) return body;
    return root;
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
    const qsizetype colon = cleaned.indexOf(QStringLiteral("://"));
    if (colon == -1) return QString();
    const QString scheme = cleaned.left(colon).toLower();
    if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https"))
        return QString();
    // A netloc is required (chars between "://" and the next '/').
    if (colon + 3 >= cleaned.size()) return QString();
    if (cleaned.at(colon + 3) == QLatin1Char('/')) return QString();

    QString out;
    for (QChar ch : cleaned) {
        if (ch == QLatin1Char(')'))
            out += QStringLiteral("%29");
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
            .replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    }
    const QString name = tag_name(el);
    if (name == QStringLiteral("br")) return QStringLiteral("\n");
    if (name == QStringLiteral("a")) {
        const QString href  = safe_markdown_href(attr(el, QStringLiteral("href")));
        const QString label = safe_markdown_label(get_text(el, QStringLiteral(" "), true));
        if (!href.isEmpty() && !label.isEmpty())
            return QStringLiteral("[") + label + QStringLiteral("](") + href +
                   QStringLiteral(")");
        return label;
    }
    if (name == QStringLiteral("code"))
        return QStringLiteral("`") + get_text(el, QString(), false) + QStringLiteral("`");
    if (name == QStringLiteral("strong") || name == QStringLiteral("b")) {
        const QString inner = get_text(el, QStringLiteral(" "), true);
        return inner.isEmpty() ? QString() : QStringLiteral("**") + inner + QStringLiteral("**");
    }
    if (name == QStringLiteral("em") || name == QStringLiteral("i")) {
        const QString inner = get_text(el, QStringLiteral(" "), true);
        return inner.isEmpty() ? QString() : QStringLiteral("*") + inner + QStringLiteral("*");
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
                        get_text(el, QStringLiteral(" "), true));
        parts.push_back(QString());
    } else if (name == QStringLiteral("p")) {
        const QString text = inline_markdown(el).trimmed();
        if (!text.isEmpty()) {
            parts.push_back(text);
            parts.push_back(QString());
        }
    } else if (name == QStringLiteral("pre")) {
        QString code = get_text(el, QString(), false);
        if (!code.isEmpty() && code.endsWith(QLatin1Char('\n'))) code.chop(1);
        parts.push_back(QStringLiteral("```"));
        parts.push_back(code);
        parts.push_back(QStringLiteral("```"));
        parts.push_back(QString());
    } else if (name == QStringLiteral("ul") || name == QStringLiteral("ol")) {
        int i = 1;
        for (auto* li : find_all(el, QStringLiteral("li"), false)) {
            const QString bullet = (name == QStringLiteral("ol"))
                                       ? QString::number(i) + QLatin1Char('.')
                                       : QStringLiteral("-");
            parts.push_back(bullet + QLatin1Char(' ') +
                            get_text(li, QStringLiteral(" "), true));
            ++i;
        }
        parts.push_back(QString());
    } else if (name == QStringLiteral("blockquote")) {
        const QString quote = get_text(el, QStringLiteral(" "), true);
        if (!quote.isEmpty()) {
            parts.push_back(QStringLiteral("> ") + quote);
            parts.push_back(QString());
        }
    } else if (name == QStringLiteral("hr")) {
        parts.push_back(QStringLiteral("---"));
        parts.push_back(QString());
    } else {
        for (lxb_dom_node_t* child = el->first_child; child; child = child->next) {
            render_markdown(child, parts);
        }
    }
}

QString html_to_markdown(lxb_dom_node_t* root) {
    QStringList parts;
    render_markdown(root, parts);
    QString text = parts.join(QLatin1Char('\n'));
    text.replace(QRegularExpression(QStringLiteral("\\n{3,}")), QStringLiteral("\n\n"));
    return text.trimmed();
}

}  // namespace

QString html_to_text(const QString& html, const QString& mode, const QString& base_url) {
    if (mode != QStringLiteral("text") && mode != QStringLiteral("main") &&
        mode != QStringLiteral("markdown")) {
        throw InvalidArgument(
            QStringLiteral("Unknown parse mode '%1'. Supported: text, main, markdown")
                .arg(mode));
    }

    Article article = extract_article(html, base_url);
    if (article.ok) {
        if (mode == QStringLiteral("markdown")) return html_to_markdown(article.content);
        return collapse_whitespace(article.text_content);
    }

    // Readability needs a body with readable content. For tiny/structural
    // documents fall back to the original main-content selector heuristic.
    Document doc = Document::parse(html);
    if (!doc.valid()) return QString();

    lxb_dom_node_t* root = doc.root();
    if (mode == QStringLiteral("text")) {
        strip_chrome(root, kTextChromeTags);
        return collapse_whitespace(get_text(root, QString(), false));
    }
    strip_chrome(root, kMainChromeTags);
    lxb_dom_node_t* content = select_main_root(root);
    if (mode == QStringLiteral("main")) {
        return collapse_whitespace(get_text(content, QString(), false));
    }
    return html_to_markdown(content);
}

}  // namespace ddg::html
