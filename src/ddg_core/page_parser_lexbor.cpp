// SPDX-License-Identifier: Apache-2.0
//
// Lexbor-backed page parser. Compiled only when DDG_USE_LEXBOR is enabled.

#include "searcher.hpp"

#include "html_document.hpp"
#include "html_query.hpp"
#include "html_text.hpp"

namespace ddg {

std::vector<SearchResult> parse_search_results(const std::string& html, int max_results) {
    std::vector<SearchResult> results;

    html::Document doc = html::Document::parse(html);
    if (!doc.valid()) return results;
    lxb_dom_node_t* root = doc.root();

    for (lxb_dom_node_t* result : html::select(root, ".result")) {
        lxb_dom_node_t* title_elem = html::select_one(result, ".result__title");
        if (!title_elem) continue;
        lxb_dom_node_t* link_elem = html::find(title_elem, "a");
        if (!link_elem) continue;

        const std::string title = html::get_text(link_elem, "", true);
        std::string       link  = html::attr(link_elem, "href");

        if (link.find("y.js") != std::string::npos) continue;  // skip ad results
        link = decode_uddg(link);

        lxb_dom_node_t*   snippet_elem = html::select_one(result, ".result__snippet");
        const std::string snippet =
            snippet_elem ? html::get_text(snippet_elem, "", true) : "";

        results.push_back(
            SearchResult{title, link, snippet, static_cast<int>(results.size()) + 1});
        if (static_cast<int>(results.size()) >= max_results) break;
    }
    return results;
}

std::vector<SearchResult> parse_lite_results(const std::string& html, int max_results) {
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

        const std::string title = html::get_text(anchor, "", true);
        std::string       link  = html::attr(anchor, "href");
        if (link.find("y.js") != std::string::npos) continue;  // skip ad results
        if (link.find("google.com/search?q=") != std::string::npos) continue;
        link = decode_uddg(link);

        lxb_dom_node_t*   snippet_elem = html::select_one(snippet_row, ".result-snippet");
        const std::string snippet =
            snippet_elem ? html::get_text(snippet_elem, "", true) : "";

        results.push_back(
            SearchResult{title, link, snippet, static_cast<int>(results.size()) + 1});
        if (static_cast<int>(results.size()) >= max_results) break;
    }
    return results;
}

LiteNextPage extract_lite_next_page(const std::string& html) {
    LiteNextPage next;

    html::Document doc = html::Document::parse(html);
    if (!doc.valid()) return next;
    lxb_dom_node_t* root = doc.root();

    if (lxb_dom_node_t* s_input = html::select_one(root, "form input[name='s']")) {
        next.s = html::attr(s_input, "value");
    }
    if (lxb_dom_node_t* vqd_input = html::select_one(root, "form input[name='vqd']")) {
        next.vqd = html::attr(vqd_input, "value");
    }
    return next;
}

}  // namespace ddg
