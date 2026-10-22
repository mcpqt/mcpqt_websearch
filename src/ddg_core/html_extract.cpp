// SPDX-License-Identifier: Apache-2.0

#include "html_extract.hpp"

#include "html_document.hpp"
#include "html_query.hpp"
#include "html_text.hpp"
#include "utils.hpp"

#include <cctype>
#include <regex>
#include <stdexcept>
#include <vector>

namespace ddg::html {
namespace {

const char* const kMainSelectors[] = {
    "article", "main", "[role='main']", "#content", "#main", "#main-content",
    ".post-content", ".entry-content", ".article-body", ".article-content",
    ".post-body", ".markdown-body",
};

const std::vector<std::string> kTextChromeTags = {"script", "style", "nav", "header", "footer"};
const std::vector<std::string> kMainChromeTags = {"script", "style", "nav", "header",
                                                  "footer", "aside", "form", "noscript"};

void strip_chrome(lxb_dom_node_t* root, const std::vector<std::string>& tags) {
    std::vector<lxb_dom_node_t*> to_remove;
    for (const auto& t : tags) {
        auto found = find_all(root, t, true);
        to_remove.insert(to_remove.end(), found.begin(), found.end());
    }
    for (auto* node : to_remove) decompose(node);
}

lxb_dom_node_t* select_main_root(lxb_dom_node_t* root) {
    for (const char* selector : kMainSelectors) {
        auto* found = select_one(root, selector);
        if (found && get_text(found, " ", true).size() >= 40) return found;
    }
    if (auto* body = find(root, "body")) return body;
    return root;
}

std::string safe_markdown_href(const std::string& href) {
    std::string cleaned;
    for (char ch : href) {
        const unsigned char uc = static_cast<unsigned char>(ch);
        if (uc >= ' ' && ch != '\r' && ch != '\n') cleaned += ch;
    }
    cleaned = trim(std::move(cleaned));  // `.strip()`
    if (cleaned.empty()) return {};
    for (char ch : cleaned) {
        if (std::isspace(static_cast<unsigned char>(ch))) return {};
    }
    const std::size_t colon = cleaned.find("://");
    if (colon == std::string::npos) return {};
    const std::string scheme = to_lower(cleaned.substr(0, colon));
    if (scheme != "http" && scheme != "https") return {};
    // A netloc is required (chars between "://" and the next '/').
    if (colon + 3 >= cleaned.size()) return {};
    if (cleaned[colon + 3] == '/') return {};

    std::string out;
    for (char ch : cleaned) out += (ch == ')') ? "%29" : std::string(1, ch);
    return out;
}

std::string safe_markdown_label(const std::string& label) {
    std::string out;
    for (char ch : label) {
        if (ch == '\r' || ch == '\n' || ch == '[' || ch == ']') continue;
        out += ch;
    }
    return trim(std::move(out));
}

std::string inline_markdown(lxb_dom_node_t* el);

std::string inline_markdown(lxb_dom_node_t* el) {
    if (is_text(el)) {
        return std::regex_replace(get_text(el, "", false), std::regex("\\s+"), " ");
    }
    const std::string name = tag_name(el);
    if (name == "br") return "\n";
    if (name == "a") {
        const std::string href  = safe_markdown_href(attr(el, "href"));
        const std::string label = safe_markdown_label(get_text(el, " ", true));
        if (!href.empty() && !label.empty()) return "[" + label + "](" + href + ")";
        return label;
    }
    if (name == "code") return "`" + get_text(el, "", false) + "`";
    if (name == "strong" || name == "b") {
        const std::string inner = get_text(el, " ", true);
        return inner.empty() ? std::string{} : "**" + inner + "**";
    }
    if (name == "em" || name == "i") {
        const std::string inner = get_text(el, " ", true);
        return inner.empty() ? std::string{} : "*" + inner + "*";
    }
    std::string out;
    for (lxb_dom_node_t* child = el->first_child; child; child = child->next) {
        out += inline_markdown(child);
    }
    return out;
}

void render_markdown(lxb_dom_node_t* el, std::vector<std::string>& parts) {
    if (is_text(el)) {
        std::string text = trim(get_text(el, "", false));
        if (!text.empty()) parts.push_back(std::move(text));
        return;
    }
    const std::string name = tag_name(el);
    if (name.size() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6') {
        parts.push_back(std::string(name[1] - '0', '#') + " " + get_text(el, " ", true));
        parts.push_back("");
    } else if (name == "p") {
        std::string text = trim(inline_markdown(el));
        if (!text.empty()) {
            parts.push_back(std::move(text));
            parts.push_back("");
        }
    } else if (name == "pre") {
        std::string code = get_text(el, "", false);
        if (!code.empty() && code.back() == '\n') code.pop_back();
        parts.push_back("```");
        parts.push_back(code);
        parts.push_back("```");
        parts.push_back("");
    } else if (name == "ul" || name == "ol") {
        int i = 1;
        for (auto* li : find_all(el, "li", false)) {
            const std::string bullet = (name == "ol") ? std::to_string(i) + "." : "-";
            parts.push_back(bullet + " " + get_text(li, " ", true));
            ++i;
        }
        parts.push_back("");
    } else if (name == "blockquote") {
        std::string quote = get_text(el, " ", true);
        if (!quote.empty()) {
            parts.push_back("> " + quote);
            parts.push_back("");
        }
    } else if (name == "hr") {
        parts.push_back("---");
        parts.push_back("");
    } else {
        for (lxb_dom_node_t* child = el->first_child; child; child = child->next) {
            render_markdown(child, parts);
        }
    }
}

std::string html_to_markdown(lxb_dom_node_t* root) {
    std::vector<std::string> parts;
    render_markdown(root, parts);
    std::string text;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) text += "\n";
        text += parts[i];
    }
    text = std::regex_replace(text, std::regex("\\n{3,}"), "\n\n");
    return trim(std::move(text));
}

}  // namespace

std::string html_to_text(const std::string& html, const std::string& mode) {
    if (mode != "text" && mode != "main" && mode != "markdown") {
        throw std::invalid_argument("Unknown parse mode '" + mode +
                                    "'. Supported: text, main, markdown");
    }
    Document doc = Document::parse(html);
    if (!doc.valid()) return {};

    lxb_dom_node_t* root = doc.root();
    if (mode == "text") {
        strip_chrome(root, kTextChromeTags);
        return collapse_whitespace(get_text(root, "", false));
    }
    strip_chrome(root, kMainChromeTags);
    lxb_dom_node_t* content = select_main_root(root);
    if (mode == "main") {
        return collapse_whitespace(get_text(content, "", false));
    }
    return html_to_markdown(content);
}

}  // namespace ddg::html
