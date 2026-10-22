// SPDX-License-Identifier: Apache-2.0

#include "html_query.hpp"

#include <lexbor/css/css.h>
#include <lexbor/selectors/selectors.h>

#include <cstring>

namespace ddg::html {
namespace {

lxb_status_t collect_cb(lxb_dom_node_t* node, lxb_css_selector_specificity_t, void* ctx) {
    static_cast<std::vector<lxb_dom_node_t*>*>(ctx)->push_back(node);
    return LXB_STATUS_OK;
}

std::string to_str(const lxb_char_t* data, size_t len) {
    if (!data) return {};
    return std::string(reinterpret_cast<const char*>(data), len);
}

bool tag_is(lxb_dom_node_t* node, const std::string& name) {
    if (!is_element(node)) return false;
    size_t            len = 0;
    const lxb_char_t* ln  = lxb_dom_element_local_name(lxb_dom_interface_element(node), &len);
    return ln && len == name.size() && std::memcmp(ln, name.data(), len) == 0;
}

}  // namespace

std::vector<lxb_dom_node_t*> select(lxb_dom_node_t* root, const std::string& selector) {
    std::vector<lxb_dom_node_t*> out;
    if (!root || selector.empty()) return out;

    lxb_css_parser_t* parser = lxb_css_parser_create();
    if (!parser) return out;
    if (lxb_css_parser_init(parser, nullptr) != LXB_STATUS_OK) {
        lxb_css_parser_destroy(parser, true);
        return out;
    }

    lxb_css_selector_list_t* list = lxb_css_selectors_parse(
        parser, reinterpret_cast<const lxb_char_t*>(selector.data()), selector.size());
    if (!list || parser->status != LXB_STATUS_OK) {
        if (list) lxb_css_selector_list_destroy_memory(list);
        lxb_css_parser_destroy(parser, true);
        return out;
    }

    lxb_selectors_t* selectors = lxb_selectors_create();
    if (selectors && lxb_selectors_init(selectors) == LXB_STATUS_OK) {
        lxb_selectors_find(selectors, root, list, collect_cb, &out);
        lxb_selectors_destroy(selectors, true);
    } else if (selectors) {
        lxb_selectors_destroy(selectors, true);
    }

    lxb_css_parser_destroy(parser, true);
    lxb_css_selector_list_destroy_memory(list);
    return out;
}

lxb_dom_node_t* select_one(lxb_dom_node_t* root, const std::string& selector) {
    auto matches = select(root, selector);
    return matches.empty() ? nullptr : matches.front();
}

std::vector<lxb_dom_node_t*> find_all(lxb_dom_node_t* node, const std::string& name,
                                      bool recursive) {
    if (!node) return {};
    if (recursive) return select(node, name);

    std::vector<lxb_dom_node_t*> out;
    for (lxb_dom_node_t* c = node->first_child; c; c = c->next) {
        if (tag_is(c, name)) out.push_back(c);
    }
    return out;
}

lxb_dom_node_t* find(lxb_dom_node_t* node, const std::string& name) {
    if (!node) return nullptr;
    if (tag_is(node, name)) return node;
    for (lxb_dom_node_t* c = node->first_child; c; c = c->next) {
        if (lxb_dom_node_t* hit = find(c, name)) return hit;
    }
    return nullptr;
}

std::string tag_name(lxb_dom_node_t* node) {
    if (!is_element(node)) return {};
    size_t            len = 0;
    const lxb_char_t* ln  = lxb_dom_element_local_name(lxb_dom_interface_element(node), &len);
    return to_str(ln, len);
}

bool is_element(lxb_dom_node_t* node) {
    return node && node->type == LXB_DOM_NODE_TYPE_ELEMENT;
}

bool is_text(lxb_dom_node_t* node) {
    return node && (node->type == LXB_DOM_NODE_TYPE_TEXT ||
                    node->type == LXB_DOM_NODE_TYPE_CDATA_SECTION);
}

std::string attr(lxb_dom_node_t* node, const std::string& name) {
    if (!is_element(node)) return {};
    size_t            len = 0;
    const lxb_char_t* v   = lxb_dom_element_get_attribute(
        lxb_dom_interface_element(node),
        reinterpret_cast<const lxb_char_t*>(name.data()), name.size(), &len);
    return to_str(v, len);
}

bool has_attr(lxb_dom_node_t* node, const std::string& name) {
    if (!is_element(node)) return false;
    return lxb_dom_element_has_attribute(
        lxb_dom_interface_element(node),
        reinterpret_cast<const lxb_char_t*>(name.data()), name.size());
}

}  // namespace ddg::html
