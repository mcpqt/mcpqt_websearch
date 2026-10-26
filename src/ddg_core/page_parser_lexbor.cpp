// SPDX-License-Identifier: AGPL-3.0-only
//
// Lexbor-backed page parser. Compiled only when DDG_USE_LEXBOR is enabled.

#include "searcher.hpp"

#include "html_document.hpp"
#include "html_query.hpp"
#include "html_text.hpp"

#include <QString>

namespace ddg {

std::vector<SearchResult> parse_search_results(const QString& html, int max_results) {
    std::vector<SearchResult> results;

    html::Document doc = html::Document::parse(html);
    if (!doc.valid()) return results;
    lxb_dom_node_t* root = doc.root();

    for (lxb_dom_node_t* result : html::select(root, QStringLiteral(".result"))) {
        lxb_dom_node_t* title_elem = html::select_one(result, QStringLiteral(".result__title"));
        if (!title_elem) continue;
        lxb_dom_node_t* link_elem = html::find(title_elem, QStringLiteral("a"));
        if (!link_elem) continue;

        const QString title = html::get_text(link_elem, QString(), true);
        QString       link  = html::attr(link_elem, QStringLiteral("href"));

        if (link.contains(QStringLiteral("y.js"))) continue;  // skip ad results
        link = decode_uddg(link);

        lxb_dom_node_t* snippet_elem =
            html::select_one(result, QStringLiteral(".result__snippet"));
        const QString snippet =
            snippet_elem ? html::get_text(snippet_elem, QString(), true) : QString();

        results.push_back(
            SearchResult{title, link, snippet, static_cast<int>(results.size()) + 1});
        if (static_cast<int>(results.size()) >= max_results) break;
    }
    return results;
}

std::vector<SearchResult> parse_lite_results(const QString& html, int max_results) {
    std::vector<SearchResult> results;

    html::Document doc = html::Document::parse(html);
    if (!doc.valid()) return results;
    lxb_dom_node_t* root = doc.root();

    const auto tables = html::select(root, QStringLiteral("table"));
    if (tables.empty()) return results;
    lxb_dom_node_t* last_table = tables.back();
    const auto      rows       = html::select(last_table, QStringLiteral("tr"));

    // The lite layout uses four rows per hit: title, snippet, URL preview, separator.
    for (std::size_t i = 0; i + 1 < rows.size(); i += 4) {
        lxb_dom_node_t* title_row   = rows[i];
        lxb_dom_node_t* snippet_row = rows[i + 1];

        lxb_dom_node_t* anchor = html::find(title_row, QStringLiteral("a"));
        if (!anchor) continue;

        const QString title = html::get_text(anchor, QString(), true);
        QString       link  = html::attr(anchor, QStringLiteral("href"));
        if (link.contains(QStringLiteral("y.js"))) continue;  // skip ad results
        if (link.contains(QStringLiteral("google.com/search?q="))) continue;
        link = decode_uddg(link);

        lxb_dom_node_t* snippet_elem =
            html::select_one(snippet_row, QStringLiteral(".result-snippet"));
        const QString snippet =
            snippet_elem ? html::get_text(snippet_elem, QString(), true) : QString();

        results.push_back(
            SearchResult{title, link, snippet, static_cast<int>(results.size()) + 1});
        if (static_cast<int>(results.size()) >= max_results) break;
    }
    return results;
}

LiteNextPage extract_lite_next_page(const QString& html) {
    LiteNextPage next;

    html::Document doc = html::Document::parse(html);
    if (!doc.valid()) return next;
    lxb_dom_node_t* root = doc.root();

    if (lxb_dom_node_t* s_input =
            html::select_one(root, QStringLiteral("form input[name='s']"))) {
        next.s = html::attr(s_input, QStringLiteral("value"));
    }
    if (lxb_dom_node_t* vqd_input =
            html::select_one(root, QStringLiteral("form input[name='vqd']"))) {
        next.vqd = html::attr(vqd_input, QStringLiteral("value"));
    }
    return next;
}

}  // namespace ddg
