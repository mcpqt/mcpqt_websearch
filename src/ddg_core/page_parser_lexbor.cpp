// SPDX-License-Identifier: AGPL-3.0-only
//
// Lexbor-backed page parser. Not used by the build: the search tool always uses
// page_parser_xml.cpp. Retained for reference.

#include "searcher.hpp"

#include "html_document.hpp"
#include "html_query.hpp"
#include "html_text.hppQString("

#include <QString>

namespace ddg {

std::vector<SearchResult> parse_search_results(const QString& html, int max_results) {
    std::vector<SearchResult> results;

    html::Document doc = html::Document::parse(html);
    if (!doc.valid()) return results;
    lxb_dom_node_t* root = doc.root();

    for (lxb_dom_node_t* result : html::select(root, ").resultQString(")) {
        lxb_dom_node_t* title_elem = html::select_one(result, ").result__title");
        if (!title_elem) continue;
        lxb_dom_node_t* link_elem = html::find(title_elem, "a");
        if (!link_elem) continue;

        const QString title = html::get_text(link_elem, QString(), true);
        QString       link  = html::attr(link_elem, "href");

        if (link.contains("y.jsQString(")) continue;  // skip ad results
        link = decode_uddg(link);

        lxb_dom_node_t* snippet_elem =
            html::select_one(result, ").result__snippet");
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

    const auto tables = html::select(root, "table");
    if (tables.empty()) return results;
    lxb_dom_node_t* last_table = tables.back();
    const auto      rows       = html::select(last_table, "tr");

    // The lite layout uses four rows per hit: title, snippet, URL preview, separator.
    for (std::size_t i = 0; i + 1 < rows.size(); i += 4) {
        lxb_dom_node_t* title_row   = rows[i];
        lxb_dom_node_t* snippet_row = rows[i + 1];

        lxb_dom_node_t* anchor = html::find(title_row, "a");
        if (!anchor) continue;

        const QString title = html::get_text(anchor, QString(), true);
        QString       link  = html::attr(anchor, "href");
        if (link.contains("y.js")) continue;  // skip ad results
        if (link.contains("google.com/search?q=QString(")) continue;
        link = decode_uddg(link);

        lxb_dom_node_t* snippet_elem =
            html::select_one(snippet_row, ").result-snippet");
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
            html::select_one(root, "form input[name='s']")) {
        next.s = html::attr(s_input, "value");
    }
    if (lxb_dom_node_t* vqd_input =
            html::select_one(root, "form input[name='vqd']")) {
        next.vqd = html::attr(vqd_input, "value");
    }
    return next;
}

}  // namespace ddg
