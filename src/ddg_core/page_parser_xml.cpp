// SPDX-License-Identifier: AGPL-3.0-only
//
// QXmlStreamReader-backed page parser. The search tool always uses this parser.

#include "searcher.hpp"

#include "xml_dom.hpp"

#include <QString>

namespace ddg {
namespace {

const xml::Node* find_input_by_name(const xml::Node* node, const QString& name) {
    if (!node) return nullptr;
    for (const auto& child : node->children) {
        if (child->is_text()) continue;
        if (child->tag == QStringLiteral("input") &&
            xml::attr(child.get(), QStringLiteral("name")) == name) {
            return child.get();
        }
        if (const auto* found = find_input_by_name(child.get(), name)) return found;
    }
    return nullptr;
}

}  // namespace

std::vector<SearchResult> parse_search_results(const QString& html, int max_results) {
    std::vector<SearchResult> results;

    const auto doc = xml::parse(html);
    if (!doc) return results;

    for (const xml::Node* result : xml::select_class(doc.get(), QStringLiteral("result"))) {
        const xml::Node* title_elem =
            xml::select_one_class(result, QStringLiteral("result__title"));
        if (!title_elem) continue;
        const xml::Node* link_elem = xml::find_first(title_elem, QStringLiteral("a"));
        if (!link_elem) continue;

        const QString title = xml::text(link_elem, true);
        QString       link  = xml::attr(link_elem, QStringLiteral("href"));

        if (link.contains(QStringLiteral("y.js"))) continue;  // skip ad results
        link = decode_uddg(link);

        const xml::Node* snippet_elem =
            xml::select_one_class(result, QStringLiteral("result__snippet"));
        const QString snippet =
            snippet_elem ? xml::text(snippet_elem, true) : QString();

        results.push_back(
            SearchResult{title, link, snippet, static_cast<int>(results.size()) + 1});
        if (static_cast<int>(results.size()) >= max_results) break;
    }
    return results;
}

std::vector<SearchResult> parse_lite_results(const QString& html, int max_results) {
    std::vector<SearchResult> results;

    const auto doc = xml::parse(html);
    if (!doc) return results;

    const auto tables = xml::find_all(doc.get(), QStringLiteral("table"));
    if (tables.empty()) return results;
    const xml::Node* last_table = tables.back();
    const auto       rows       = xml::find_all(last_table, QStringLiteral("tr"));

    // The lite layout uses four rows per hit: title, snippet, URL preview, separator.
    for (std::size_t i = 0; i + 1 < rows.size(); i += 4) {
        const xml::Node* title_row   = rows[i];
        const xml::Node* snippet_row = rows[i + 1];

        const xml::Node* anchor = xml::find_first(title_row, QStringLiteral("a"));
        if (!anchor) continue;

        const QString title = xml::text(anchor, true);
        QString       link  = xml::attr(anchor, QStringLiteral("href"));
        if (link.contains(QStringLiteral("y.js"))) continue;  // skip ad results
        if (link.contains(QStringLiteral("google.com/search?q="))) continue;
        link = decode_uddg(link);

        const xml::Node* snippet_elem =
            xml::select_one_class(snippet_row, QStringLiteral("result-snippet"));
        const QString snippet =
            snippet_elem ? xml::text(snippet_elem, true) : QString();

        results.push_back(
            SearchResult{title, link, snippet, static_cast<int>(results.size()) + 1});
        if (static_cast<int>(results.size()) >= max_results) break;
    }
    return results;
}

LiteNextPage extract_lite_next_page(const QString& html) {
    LiteNextPage next;

    const auto doc = xml::parse(html);
    if (!doc) return next;

    if (const auto* s_input = find_input_by_name(doc.get(), QStringLiteral("s"))) {
        next.s = xml::attr(s_input, QStringLiteral("value"));
    }
    if (const auto* vqd_input = find_input_by_name(doc.get(), QStringLiteral("vqd"))) {
        next.vqd = xml::attr(vqd_input, QStringLiteral("value"));
    }
    return next;
}

}  // namespace ddg
