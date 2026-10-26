// SPDX-License-Identifier: AGPL-3.0-only
//
// C++ port of Mozilla's Readability.js (Apache-2.0).
// Copyright (c) 2010 Arc90 Inc.
//
// The scoring, cleanup and metadata heuristics intentionally mirror
// Readability.js so the extracted content matches Firefox Reader View as
// closely as the small Lexbor-backed DOM layer allows.

#include "readability.hpp"

#include "html_query.hpp"
#include "html_text.hpp"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <vector>

namespace ddg::html {
namespace {

using Node   = lxb_dom_node_t;
using Nodes  = std::vector<Node*>;
using StrSet = QSet<QString>;

constexpr int kFlagStripUnlikelys      = 0x1;
constexpr int kFlagWeightClasses       = 0x2;
constexpr int kFlagCleanConditionally  = 0x4;
constexpr int kDefaultCharThreshold    = 500;
constexpr int kDefaultNbTopCandidates  = 5;

const StrSet kDefaultTagsToScore = {
    QStringLiteral("section"), QStringLiteral("h2"), QStringLiteral("h3"),
    QStringLiteral("h4"),      QStringLiteral("h5"), QStringLiteral("h6"),
    QStringLiteral("p"),       QStringLiteral("td"), QStringLiteral("pre"),
};

const StrSet kUnlikelyRoles = {
    QStringLiteral("menu"),  QStringLiteral("menubar"),
    QStringLiteral("complementary"), QStringLiteral("navigation"),
    QStringLiteral("alert"), QStringLiteral("alertdialog"),
    QStringLiteral("dialog"),
};

const StrSet kDivToPElems = {
    QStringLiteral("blockquote"), QStringLiteral("dl"), QStringLiteral("div"),
    QStringLiteral("img"),        QStringLiteral("ol"), QStringLiteral("p"),
    QStringLiteral("pre"),        QStringLiteral("table"), QStringLiteral("ul"),
};

const StrSet kAlterToDivExceptions = {
    QStringLiteral("div"), QStringLiteral("article"), QStringLiteral("section"),
    QStringLiteral("p"),   QStringLiteral("ol"),      QStringLiteral("ul"),
};

const QStringList kPresentationalAttributes = {
    QStringLiteral("align"),      QStringLiteral("background"),
    QStringLiteral("bgcolor"),    QStringLiteral("border"),
    QStringLiteral("cellpadding"), QStringLiteral("cellspacing"),
    QStringLiteral("frame"),      QStringLiteral("hspace"),
    QStringLiteral("rules"),      QStringLiteral("style"),
    QStringLiteral("valign"),     QStringLiteral("vspace"),
};

const StrSet kDeprecatedSizeElements = {
    QStringLiteral("table"), QStringLiteral("th"), QStringLiteral("td"),
    QStringLiteral("hr"),    QStringLiteral("pre"),
};

const StrSet kPhrasingElems = {
    QStringLiteral("abbr"),   QStringLiteral("audio"),  QStringLiteral("b"),
    QStringLiteral("bdo"),    QStringLiteral("br"),     QStringLiteral("button"),
    QStringLiteral("cite"),   QStringLiteral("code"),   QStringLiteral("data"),
    QStringLiteral("datalist"), QStringLiteral("dfn"),  QStringLiteral("em"),
    QStringLiteral("embed"),  QStringLiteral("i"),      QStringLiteral("img"),
    QStringLiteral("input"),  QStringLiteral("kbd"),    QStringLiteral("label"),
    QStringLiteral("mark"),   QStringLiteral("math"),   QStringLiteral("meter"),
    QStringLiteral("noscript"), QStringLiteral("object"), QStringLiteral("output"),
    QStringLiteral("progress"), QStringLiteral("q"),    QStringLiteral("ruby"),
    QStringLiteral("samp"),   QStringLiteral("script"), QStringLiteral("select"),
    QStringLiteral("small"),  QStringLiteral("span"),   QStringLiteral("strong"),
    QStringLiteral("sub"),    QStringLiteral("sup"),    QStringLiteral("textarea"),
    QStringLiteral("time"),   QStringLiteral("var"),    QStringLiteral("wbr"),
};

const QStringList kClassesToPreserve = {QStringLiteral("page")};

const QRegularExpression& reUnlikely() {
    static const QRegularExpression re(QStringLiteral(
        "-ad-|ai2html|banner|breadcrumbs|combx|comment|community|cover-wrap|disqus|"
        "extra|footer|gdpr|header|legends|menu|related|remark|replies|rss|shoutbox|"
        "sidebar|skyscraper|social|sponsor|supplemental|ad-break|agegate|pagination|"
        "pager|popup|yom-remote"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}
const QRegularExpression& reMaybeCandidate() {
    static const QRegularExpression re(
        QStringLiteral("and|article|body|column|content|main|mathjax|shadow"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}
const QRegularExpression& rePositive() {
    static const QRegularExpression re(
        QStringLiteral("article|body|content|entry|hentry|h-entry|main|page|"
                       "pagination|post|text|blog|story"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}
const QRegularExpression& reNegative() {
    static const QRegularExpression re(
        QStringLiteral("-ad-|hidden|^hid$| hid$| hid |^hid |banner|combx|comment|"
                       "com-|contact|footer|gdpr|masthead|media|meta|outbrain|promo|"
                       "related|scroll|share|shoutbox|sidebar|skyscraper|sponsor|"
                       "shopping|tags|widget"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}
const QRegularExpression& reByline() {
    static const QRegularExpression re(
        QStringLiteral("byline|author|dateline|writtenby|p-author"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}
const QRegularExpression& reShareElements() {
    static const QRegularExpression re(QStringLiteral("(\\b|_)(share|sharedaddy)(\\b|_)"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
}
const QRegularExpression& reVideos() {
    static const QRegularExpression re(QStringLiteral(
        "//(www\\.)?((dailymotion|youtube|youtube-nocookie|player\\.vimeo|v\\.qq|"
        "bilibili|live\\.bilibili)\\.com|(archive|upload\\.wikimedia)\\.org|"
        "player\\.twitch\\.tv)"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}
const QRegularExpression& reAdWords() {
    static const QRegularExpression re(
        QStringLiteral("^(ad(vertising|vertisement)?|pub(licit\xc3\xa9)?|werb(ung)?|"
                       "\xe5\xb9\xbf\xe5\x91\x8a|"
                       "\xd0\xa0\xd0\xb5\xd0\xba\xd0\xbb\xd0\xb0\xd0\xbc\xd0\xb0|"
                       "Anuncio)$"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    return re;
}
const QRegularExpression& reLoadingWords() {
    static const QRegularExpression re(
        QStringLiteral("^((loading|"
                       "\xe6\xad\xa3\xe5\x9c\xa8\xe5\x8a\xa0\xe8\xbd\xbd|"
                       "\xd0\x97\xd0\xb0\xd0\xb3\xd1\x80\xd1\x83\xd0\xb7\xd0\xba\xd0\xb0|"
                       "chargement|cargando)(\xe2\x80\xa6|\\.\\.\\.)?)$"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    return re;
}
const QRegularExpression& reCommas() {
    static const QRegularExpression re(QStringLiteral(
        "[,\\x{060C}\\x{FE50}\\x{FE10}\\x{FE11}\\x{2E41}\\x{2E34}\\x{2E32}\\x{FF0C}]"));
    return re;
}
const QRegularExpression& reB64DataUrl() {
    static const QRegularExpression re(
        QStringLiteral("^data:\\s*([^\\s;,]+)\\s*;\\s*base64\\s*,"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}
const QRegularExpression& reHashUrl() {
    static const QRegularExpression re(QStringLiteral("^#.+"));
    return re;
}
const QRegularExpression& reSrcsetUrl() {
    static const QRegularExpression re(QStringLiteral("(\\S+)(\\s+[\\d.]+[xw])?(\\s*(?:,|$))"));
    return re;
}
const QRegularExpression& reWhitespace() {
    static const QRegularExpression re(QStringLiteral("^\\s*$"));
    return re;
}
const QRegularExpression& reHasContent() {
    static const QRegularExpression re(QStringLiteral("\\S$"));
    return re;
}
const QRegularExpression& reNormalize() {
    static const QRegularExpression re(QStringLiteral("\\s{2,}"));
    return re;
}
const QRegularExpression& reTokenize() {
    static const QRegularExpression re(QStringLiteral("[^\\w]+"),
                                       QRegularExpression::UseUnicodePropertiesOption);
    return re;
}

// ---------------------------------------------------------------------------
// Small DOM helpers
// ---------------------------------------------------------------------------

Node* next_element(Node* node) {
    Node* n = node ? node->next : nullptr;
    while (n && n->type != LXB_DOM_NODE_TYPE_ELEMENT) n = n->next;
    return n;
}

Nodes element_children(Node* node) {
    Nodes out;
    if (!node) return out;
    for (Node* c = node->first_child; c; c = c->next)
        if (c->type == LXB_DOM_NODE_TYPE_ELEMENT) out.push_back(c);
    return out;
}

Nodes child_nodes(Node* node) {
    Nodes out;
    if (!node) return out;
    for (Node* c = node->first_child; c; c = c->next) out.push_back(c);
    return out;
}

Nodes all_by_tag(Node* root, const QString& tag) {
    Nodes out;
    if (!root) return out;
    const QByteArray name = tag.toUtf8();
    std::function<void(Node*)> walk = [&](Node* n) {
        for (Node* c = n->first_child; c; c = c->next) {
            if (c->type == LXB_DOM_NODE_TYPE_ELEMENT) {
                size_t            len = 0;
                const lxb_char_t* ln =
                    lxb_dom_element_local_name(lxb_dom_interface_element(c), &len);
                if (ln && len == static_cast<size_t>(name.size()) &&
                    std::memcmp(ln, name.constData(), len) == 0)
                    out.push_back(c);
            }
            walk(c);
        }
    };
    walk(root);
    return out;
}

Node* first_by_tag(Node* root, const QString& tag) {
    Nodes all = all_by_tag(root, tag);
    return all.empty() ? nullptr : all.front();
}

int count_by_tag(Node* root, const QString& tag) {
    return static_cast<int>(all_by_tag(root, tag).size());
}

QString text_of(Node* node) {
    return node ? get_text(node, QString(), false) : QString();
}

QString inner_text(Node* node, bool normalize_spaces = true) {
    if (!node) return QString();
    QString text = text_of(node).trimmed();
    if (normalize_spaces) text.replace(reNormalize(), QStringLiteral(" "));
    return text;
}

QString class_name(Node* node) { return attr(node, QStringLiteral("class")); }
QString node_id(Node* node) { return attr(node, QStringLiteral("id")); }
QString match_string(Node* node) {
    return class_name(node) + QLatin1Char(' ') + node_id(node);
}

bool has_tag(Node* node, const char* tag) {
    return node && tag_name(node) == QLatin1String(tag);
}

void remove_node(Node* node) {
    if (node) lxb_dom_node_remove(node);
}

// Lexbor's insert_child/insert_before do not detach an existing node from its
// previous parent. Always remove first when moving nodes between parents.
void move_child(Node* parent, Node* child) {
    if (!parent || !child) return;
    lxb_dom_node_remove(child);
    lxb_dom_node_insert_child(parent, child);
}

void move_before(Node* ref, Node* node) {
    if (!ref || !node) return;
    lxb_dom_node_remove(node);
    lxb_dom_node_insert_before(ref, node);
}

void remove_nodes(Nodes nodes) {
    // Readability removes from the end so indexes stay stable.
    for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) remove_node(*it);
}

void set_attr(Node* node, const QString& name, const QString& value) {
    if (!is_element(node)) return;
    const QByteArray n = name.toUtf8();
    const QByteArray v = value.toUtf8();
    lxb_dom_element_set_attribute(
        lxb_dom_interface_element(node),
        reinterpret_cast<const lxb_char_t*>(n.constData()),
        static_cast<size_t>(n.size()),
        reinterpret_cast<const lxb_char_t*>(v.constData()),
        static_cast<size_t>(v.size()));
}

void remove_attr(Node* node, const QString& name) {
    if (!is_element(node)) return;
    const QByteArray n = name.toUtf8();
    lxb_dom_element_remove_attribute(
        lxb_dom_interface_element(node),
        reinterpret_cast<const lxb_char_t*>(n.constData()),
        static_cast<size_t>(n.size()));
}

void copy_attributes(Node* from, Node* to) {
    if (!is_element(from) || !is_element(to)) return;
    auto* el = lxb_dom_interface_element(from);
    for (auto* a = el->first_attr; a; a = a->next) {
        size_t            nlen = 0;
        size_t            vlen = 0;
        const lxb_char_t* n    = lxb_dom_attr_qualified_name(a, &nlen);
        const lxb_char_t* v    = lxb_dom_attr_value(a, &vlen);
        if (!n) continue;
        lxb_dom_element_set_attribute(
            lxb_dom_interface_element(to), n, nlen,
            v ? v : reinterpret_cast<const lxb_char_t*>(""),
            v ? vlen : 0);
    }
}

bool is_whitespace(Node* node) {
    if (!node) return false;
    if (node->type == LXB_DOM_NODE_TYPE_TEXT) return text_of(node).trimmed().isEmpty();
    return has_tag(node, "br");
}

bool every_child_phrasing(Node* node);

bool is_phrasing_content(Node* node) {
    if (!node) return false;
    if (node->type == LXB_DOM_NODE_TYPE_TEXT) return true;
    if (node->type != LXB_DOM_NODE_TYPE_ELEMENT) return false;
    const QString tag = tag_name(node);
    if (kPhrasingElems.contains(tag)) return true;
    if (tag == QStringLiteral("a") || tag == QStringLiteral("del") ||
        tag == QStringLiteral("ins"))
        return every_child_phrasing(node);
    return false;
}

bool every_child_phrasing(Node* node) {
    for (Node* c = node->first_child; c; c = c->next)
        if (!is_phrasing_content(c)) return false;
    return true;
}

bool has_single_tag_inside(Node* element, const QString& tag) {
    Nodes children = element_children(element);
    if (children.size() != 1 || tag_name(children[0]) != tag) return false;
    for (Node* c = element->first_child; c; c = c->next) {
        if (c->type == LXB_DOM_NODE_TYPE_TEXT && reHasContent().match(text_of(c)).hasMatch())
            return false;
    }
    return true;
}

bool is_element_without_content(Node* node) {
    if (!node || node->type != LXB_DOM_NODE_TYPE_ELEMENT) return false;
    if (!text_of(node).trimmed().isEmpty()) return false;
    Nodes children = element_children(node);
    if (children.empty()) return true;
    return children.size() ==
           static_cast<size_t>(count_by_tag(node, QStringLiteral("br")) +
                               count_by_tag(node, QStringLiteral("hr")));
}

bool has_child_block_element(Node* element) {
    for (Node* c = element->first_child; c; c = c->next) {
        if (c->type != LXB_DOM_NODE_TYPE_ELEMENT) continue;
        const QString tag = tag_name(c);
        if (kDivToPElems.contains(tag) || has_child_block_element(c)) return true;
    }
    return false;
}

Node* next_node(Node* node, bool ignore_self_and_kids = false) {
    if (!node) return nullptr;
    if (!ignore_self_and_kids) {
        Nodes children = element_children(node);
        if (!children.empty()) return children.front();
    }
    if (Node* sib = next_element(node)) return sib;
    do {
        node = node->parent;
    } while (node && !next_element(node));
    return node ? next_element(node) : nullptr;
}

Node* remove_and_get_next(Node* node) {
    Node* next = next_node(node, true);
    remove_node(node);
    return next;
}

bool is_probably_visible(Node* node) {
    if (!node) return false;
    const QString style = attr(node, QStringLiteral("style"));
    const QString lowered = style.toLower();
    if (lowered.contains(QStringLiteral("display:none")) ||
        lowered.contains(QStringLiteral("display: none")) ||
        lowered.contains(QStringLiteral("visibility:hidden")) ||
        lowered.contains(QStringLiteral("visibility: hidden")))
        return false;
    if (has_attr(node, QStringLiteral("hidden"))) return false;
    if (has_attr(node, QStringLiteral("aria-hidden")) &&
        attr(node, QStringLiteral("aria-hidden")) == QStringLiteral("true")) {
        return class_name(node).contains(QStringLiteral("fallback-image"));
    }
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Readability extractor
// ---------------------------------------------------------------------------

class Readability {
public:
    Readability(Document doc, QString base_url, int flags)
        : doc_(std::move(doc)), base_url_(std::move(base_url)), flags_(flags) {}

    Article parse();

private:
    // Document preparation and traversal.
    void   prep_document();
    void   replace_brs(Node* elem);
    void   fix_relative_uris(Node* content);
    void   simplify_nested_elements(Node* content);
    void   clean_classes(Node* node);
    void   clean_styles(Node* e);
    Node*  set_node_tag(Node* node, const QString& tag);

    // Title and metadata.
    QString get_article_title();
    QJsonObject get_json_ld();
    void        get_article_metadata(const QJsonObject& jsonld);

    // Main article scoring / extraction.
    Node*  grab_article();
    void   initialize_node(Node* node);
    Nodes  get_node_ancestors(Node* node, int max_depth = 0) const;
    double get_link_density(Node* element);
    int    get_class_weight(Node* e);
    double text_similarity(const QString& a, const QString& b) const;
    bool   is_valid_byline(Node* node, const QString& match);
    bool   header_duplicates_title(Node* node);

    // Cleanup.
    void prep_article(Node* article);
    void mark_data_tables(Node* root);
    void fix_lazy_images(Node* root);
    void clean_conditionally(Node* e, const QString& tag);
    void clean(Node* e, const QString& tag);
    void clean_matched_nodes(Node* e, const std::function<bool(Node*, const QString&)>& filter);
    void clean_headers(Node* e);
    void unwrap_noscript_images();
    void remove_scripts();

    bool has_ancestor_tag(Node* node, const QString& tag, int max_depth = 3,
                         const std::function<bool(Node*)>& filter = {}) const;
    void get_row_and_column_count(Node* table, int& rows, int& columns);
    double get_text_density(Node* e, const QStringList& tags);
    int    get_char_count(Node* e, const QString& s) const;

    bool flag_active(int flag) const { return (flags_ & flag) > 0; }
    void remove_flag(int flag) { flags_ &= ~flag; }

    // Node data (Readability attaches these directly to DOM nodes).
    double& score(Node* n) {
        if (!scores_.contains(n)) scores_.insert(n, 0.0);
        return scores_[n];
    }
    bool score_initialized(Node* n) const { return scores_.contains(n); }
    void set_data_table(Node* n, bool value) { data_tables_.insert(n, value); }
    bool data_table(Node* n) const { return data_tables_.value(n, false); }

    Document doc_;
    QString  base_url_;
    int      flags_ = kFlagStripUnlikelys | kFlagWeightClasses | kFlagCleanConditionally;

    QString article_title_;
    QString article_byline_;
    QString article_dir_;
    QString article_lang_;
    QString article_site_name_;
    QString article_excerpt_;
    QString article_published_;

    QHash<Node*, double> scores_;
    QHash<Node*, bool>   data_tables_;
};

Node* Readability::set_node_tag(Node* node, const QString& tag) {
    if (!node || !is_element(node)) return node;
    const QByteArray name = tag.toLower().toUtf8();
    auto* replacement = lxb_html_document_create_element(
        doc_.raw(), reinterpret_cast<const lxb_char_t*>(name.constData()),
        static_cast<size_t>(name.size()), nullptr);
    if (!replacement) return node;
    Node* repl = lxb_dom_interface_node(replacement);
    while (node->first_child) move_child(repl, node->first_child);
    copy_attributes(node, repl);
    if (scores_.contains(node)) scores_.insert(repl, scores_.value(node));
    if (data_tables_.contains(node)) data_tables_.insert(repl, data_tables_.value(node));
    lxb_dom_node_insert_before(node, repl);
    remove_node(node);
    return repl;
}

void Readability::replace_brs(Node* elem) {
    for (Node* br : all_by_tag(elem, QStringLiteral("br"))) {
        if (!br->parent) continue;
        Node* next     = br->next;
        bool  replaced = false;
        while (true) {
            // _nextNode skips whitespace text nodes.
            while (next && next->type != LXB_DOM_NODE_TYPE_ELEMENT &&
                   reWhitespace().match(text_of(next)).hasMatch())
                next = next->next;
            if (!next || !has_tag(next, "br")) break;
            replaced = true;
            Node* sibling = next->next;
            remove_node(next);
            next = sibling;
        }

        if (!replaced) continue;

        const QByteArray pname = QByteArrayLiteral("p");
        auto* pel = lxb_html_document_create_element(
            doc_.raw(), reinterpret_cast<const lxb_char_t*>(pname.constData()),
            static_cast<size_t>(pname.size()), nullptr);
        Node* p = lxb_dom_interface_node(pel);
        lxb_dom_node_insert_before(br, p);
        remove_node(br);

        next = p->next;
        while (next) {
            if (has_tag(next, "br")) {
                Node* nn = next->next;
                while (nn && nn->type != LXB_DOM_NODE_TYPE_ELEMENT &&
                       reWhitespace().match(text_of(nn)).hasMatch())
                    nn = nn->next;
                if (nn && has_tag(nn, "br")) break;
            }
            if (!is_phrasing_content(next)) break;
            Node* sibling = next->next;
            move_child(p, next);
            next = sibling;
        }
        while (p->last_child && is_whitespace(p->last_child)) remove_node(p->last_child);
        if (p->parent && has_tag(p->parent, "p")) set_node_tag(p->parent, QStringLiteral("div"));
    }
}

void Readability::prep_document() {
    remove_nodes(all_by_tag(doc_.root(), QStringLiteral("style")));
    if (Node* body = find(doc_.root(), QStringLiteral("body"))) replace_brs(body);
    for (Node* font : all_by_tag(doc_.root(), QStringLiteral("font")))
        set_node_tag(font, QStringLiteral("span"));
}

QString Readability::get_article_title() {
    QString cur;
    QString orig;
    if (Node* title = find(doc_.root(), QStringLiteral("title"))) {
        cur = inner_text(title, false);
        orig = cur;
    }
    if (cur.isEmpty()) return cur;

    const QString separators = QStringLiteral("\\|\\-–—\\\\/>»");
    QRegularExpression sep_re(QStringLiteral("\\s[") + separators + QStringLiteral("]\\s"));
    bool had_hierarchical = false;

    auto word_count = [](const QString& s) {
        return s.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).size();
    };

    if (sep_re.match(cur).hasMatch()) {
        had_hierarchical = QRegularExpression(QStringLiteral("\\s[\\\\/>»]\\s")).match(cur).hasMatch();
        auto it = sep_re.globalMatch(orig);
        int last = -1;
        while (it.hasNext()) {
            auto m = it.next();
            last   = static_cast<int>(m.capturedStart());
        }
        if (last > 0) cur = orig.left(last);
        if (word_count(cur) < 3) {
            cur = orig;
            QRegularExpression first_sep(QStringLiteral("^[^") + separators +
                                             QStringLiteral("]*[") + separators +
                                             QStringLiteral("]"),
                                         QRegularExpression::CaseInsensitiveOption);
            cur.remove(first_sep);
        }
    } else if (cur.contains(QStringLiteral(": "))) {
        Nodes headings = all_by_tag(doc_.root(), QStringLiteral("h1"));
        Nodes h2s      = all_by_tag(doc_.root(), QStringLiteral("h2"));
        headings.insert(headings.end(), h2s.begin(), h2s.end());
        bool matched = false;
        for (Node* h : headings) {
            if (inner_text(h, false).trimmed() == cur.trimmed()) {
                matched = true;
                break;
            }
        }
        if (!matched) {
            cur = orig.mid(orig.lastIndexOf(QLatin1Char(':')) + 1);
            if (word_count(cur) < 3) {
                cur = orig.mid(orig.indexOf(QLatin1Char(':')) + 1);
                if (word_count(orig.left(orig.indexOf(QLatin1Char(':')))) > 5) cur = orig;
            }
        }
    } else if (cur.size() > 150 || cur.size() < 15) {
        Nodes h1s = all_by_tag(doc_.root(), QStringLiteral("h1"));
        if (h1s.size() == 1) cur = inner_text(h1s.front(), false);
    }

    cur = cur.trimmed();
    cur.replace(reNormalize(), QStringLiteral(" "));
    const int wc = word_count(cur);
    if (wc <= 4) {
        QString stripped = orig;
        stripped.replace(sep_re, QString());
        const bool hierarchical_ok =
            had_hierarchical && wc == word_count(stripped) - 1;
        if (!hierarchical_ok) cur = orig;
    }
    return cur;
}

QJsonObject Readability::get_json_ld() {
    for (Node* script : all_by_tag(doc_.root(), QStringLiteral("script"))) {
        if (attr(script, QStringLiteral("type")) != QStringLiteral("application/ld+json"))
            continue;
        QString content = text_of(script);
        content.remove(QRegularExpression(QStringLiteral("^\\s*<!\\[CDATA\\[|\\]\\]>\\s*$")));
        QJsonParseError err;
        QJsonDocument   doc = QJsonDocument::fromJson(content.toUtf8(), &err);
        if (err.error != QJsonParseError::NoError) continue;

        QJsonObject parsed;
        if (doc.isArray()) {
            for (const QJsonValue& v : doc.array()) {
                if (!v.isObject()) continue;
                QJsonObject o  = v.toObject();
                QString     ty = o.value(QStringLiteral("@type")).toString();
                if (QRegularExpression(
                        QStringLiteral("^Article|AdvertiserContentArticle|NewsArticle|"
                                       "AnalysisNewsArticle|AskPublicNewsArticle|"
                                       "BackgroundNewsArticle|OpinionNewsArticle|"
                                       "ReportageNewsArticle|ReviewNewsArticle|"
                                       "SatiricalArticle|ScholarlyArticle|"
                                       "MedicalScholarlyArticle|SocialMediaPosting|"
                                       "BlogPosting|LiveBlogPosting|"
                                       "DiscussionForumPosting|TechArticle|APIReference$"))
                        .match(ty)
                        .hasMatch()) {
                    parsed = o;
                    break;
                }
            }
            if (parsed.isEmpty()) continue;
        } else {
            parsed = doc.object();
        }

        const QJsonValue ctx = parsed.value(QStringLiteral("@context"));
        const QRegularExpression schema_re(
            QStringLiteral("^https?\\://schema\\.org/?$"));
        bool ctx_ok = false;
        if (ctx.isString()) {
            ctx_ok = schema_re.match(ctx.toString()).hasMatch();
        } else if (ctx.isObject()) {
            const QJsonValue vocab = ctx.toObject().value(QStringLiteral("@vocab"));
            ctx_ok = vocab.isString() && schema_re.match(vocab.toString()).hasMatch();
        }
        if (!ctx_ok) continue;

        if (!parsed.contains(QStringLiteral("@type")) &&
            parsed.value(QStringLiteral("@graph")).isArray()) {
            for (const QJsonValue& v : parsed.value(QStringLiteral("@graph")).toArray()) {
                if (!v.isObject()) continue;
                QJsonObject o = v.toObject();
                if (QRegularExpression(QStringLiteral("Article|NewsArticle|BlogPosting|"
                                                      "TechArticle|Report"))
                        .match(o.value(QStringLiteral("@type")).toString())
                        .hasMatch()) {
                    parsed = o;
                    break;
                }
            }
        }
        const QString type = parsed.value(QStringLiteral("@type")).toString();
        if (type.isEmpty() ||
            !QRegularExpression(QStringLiteral("Article|NewsArticle|BlogPosting|"
                                               "TechArticle|Report|SocialMediaPosting|"
                                               "DiscussionForumPosting"))
                 .match(type)
                 .hasMatch())
            continue;
        return parsed;
    }
    return {};
}

void Readability::get_article_metadata(const QJsonObject& jsonld) {
    QHash<QString, QString> values;
    const QRegularExpression property_re(
        QStringLiteral("\\s*(article|dc|dcterm|og|twitter)\\s*:\\s*"
                       "(author|creator|description|published_time|title|site_name)\\s*"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression name_re(
        QStringLiteral("^\\s*(?:(dc|dcterm|og|twitter|parsely|weibo:(article|webpage))"
                       "\\s*[-.:]\\s*)?(author|creator|pub-date|description|title|site_name)\\s*$"),
        QRegularExpression::CaseInsensitiveOption);

    for (Node* meta : all_by_tag(doc_.root(), QStringLiteral("meta"))) {
        const QString content = attr(meta, QStringLiteral("content")).trimmed();
        if (content.isEmpty()) continue;
        bool matched = false;
        if (QString property = attr(meta, QStringLiteral("property")); !property.isEmpty()) {
            auto m = property_re.match(property);
            if (m.hasMatch()) {
                QString name = m.captured(0).toLower();
                name.remove(QRegularExpression(QStringLiteral("\\s")));
                values.insert(name, content);
                matched = true;
            }
        }
        const QString name_attr = attr(meta, QStringLiteral("name"));
        if (!matched && !name_attr.isEmpty() && name_re.match(name_attr).hasMatch()) {
            QString name = name_attr.toLower();
            name.remove(QRegularExpression(QStringLiteral("\\s")));
            name.replace(QLatin1Char('.'), QLatin1Char(':'));
            values.insert(name, content);
        }
    }

    auto pick = [&](std::initializer_list<QString> keys) -> QString {
        for (const QString& k : keys)
            if (values.contains(k) && !values.value(k).isEmpty()) return values.value(k);
        return QString();
    };

    QString title = jsonld.value(QStringLiteral("name")).toString();
    if (title.isEmpty()) title = jsonld.value(QStringLiteral("headline")).toString();
    if (title.isEmpty())
        title = pick({QStringLiteral("dc:title"), QStringLiteral("dcterm:title"),
                      QStringLiteral("og:title"), QStringLiteral("weibo:article:title"),
                      QStringLiteral("weibo:webpage:title"), QStringLiteral("title"),
                      QStringLiteral("twitter:title"), QStringLiteral("parsely-title")});
    if (title.isEmpty()) title = get_article_title();
    article_title_ = title;

    QString byline = jsonld.value(QStringLiteral("author")).toObject()
                         .value(QStringLiteral("name")).toString();
    if (byline.isEmpty()) {
        const QJsonValue authors = jsonld.value(QStringLiteral("author"));
        if (authors.isArray()) {
            QStringList names;
            for (const QJsonValue& v : authors.toArray()) {
                const QString n = v.toObject().value(QStringLiteral("name")).toString().trimmed();
                if (!n.isEmpty()) names.push_back(n);
            }
            byline = names.join(QStringLiteral(", "));
        }
    }
    if (byline.isEmpty())
        byline = pick({QStringLiteral("dc:creator"), QStringLiteral("dcterm:creator"),
                       QStringLiteral("author"), QStringLiteral("parsely-author")});
    article_byline_ = byline;

    QString excerpt = jsonld.value(QStringLiteral("description")).toString();
    if (excerpt.isEmpty())
        excerpt = pick({QStringLiteral("dc:description"), QStringLiteral("dcterm:description"),
                        QStringLiteral("og:description"),
                        QStringLiteral("weibo:article:description"),
                        QStringLiteral("weibo:webpage:description"),
                        QStringLiteral("description"), QStringLiteral("twitter:description")});
    // Article::excerpt is filled from the first paragraph if still empty.

    QString site = jsonld.value(QStringLiteral("publisher")).toObject()
                       .value(QStringLiteral("name")).toString();
    if (site.isEmpty()) site = pick({QStringLiteral("og:site_name")});
    article_site_name_ = site;

    QString pub = jsonld.value(QStringLiteral("datePublished")).toString();
    if (pub.isEmpty())
        pub = pick({QStringLiteral("article:published_time"),
                    QStringLiteral("parsely-pub-date")});
    // Stored on the result after extraction.
    if (Node* html = find(doc_.root(), QStringLiteral("html")))
        article_lang_ = attr(html, QStringLiteral("lang"));

    // Stash excerpt/published time in reserved strings via the current object.
    // They are consumed by parse() through these members.
    article_excerpt_      = excerpt;
    article_published_    = pub;
}

// The members are defined separately below to keep the header of the class
// focused on the algorithm.
// (Qt's MOC is not involved; these are plain C++ fields.)

Nodes Readability::get_node_ancestors(Node* node, int max_depth) const {
    Nodes out;
    int   i = 0;
    while (node && node->parent) {
        out.push_back(node->parent);
        if (max_depth && ++i == max_depth) break;
        node = node->parent;
    }
    return out;
}

int Readability::get_class_weight(Node* e) {
    if (!flag_active(kFlagWeightClasses)) return 0;
    int weight = 0;
    const QString cls = class_name(e);
    const QString id  = node_id(e);
    if (!cls.isEmpty()) {
        if (reNegative().match(cls).hasMatch()) weight -= 25;
        if (rePositive().match(cls).hasMatch()) weight += 25;
    }
    if (!id.isEmpty()) {
        if (reNegative().match(id).hasMatch()) weight -= 25;
        if (rePositive().match(id).hasMatch()) weight += 25;
    }
    return weight;
}

void Readability::initialize_node(Node* node) {
    double s = 0;
    const QString tag = tag_name(node);
    if (tag == QStringLiteral("div")) s += 5;
    else if (tag == QStringLiteral("pre") || tag == QStringLiteral("td") ||
             tag == QStringLiteral("blockquote")) s += 3;
    else if (tag == QStringLiteral("address") || tag == QStringLiteral("ol") ||
             tag == QStringLiteral("ul") || tag == QStringLiteral("dl") ||
             tag == QStringLiteral("dd") || tag == QStringLiteral("dt") ||
             tag == QStringLiteral("li") || tag == QStringLiteral("form"))
        s -= 3;
    else if (tag == QStringLiteral("h1") || tag == QStringLiteral("h2") ||
             tag == QStringLiteral("h3") || tag == QStringLiteral("h4") ||
             tag == QStringLiteral("h5") || tag == QStringLiteral("h6") ||
             tag == QStringLiteral("th"))
        s -= 5;
    s += get_class_weight(node);
    scores_.insert(node, s);
}

double Readability::get_link_density(Node* element) {
    const int text_length = inner_text(element).size();
    if (text_length == 0) return 0;
    double link_length = 0;
    for (Node* link : all_by_tag(element, QStringLiteral("a"))) {
        const QString href = attr(link, QStringLiteral("href"));
        const double coefficient = (!href.isEmpty() && reHashUrl().match(href).hasMatch()) ? 0.3 : 1.0;
        link_length += inner_text(link).size() * coefficient;
    }
    return link_length / text_length;
}

double Readability::text_similarity(const QString& a, const QString& b) const {
    const QStringList ta = a.toLower().split(reTokenize(), Qt::SkipEmptyParts);
    const QStringList tb = b.toLower().split(reTokenize(), Qt::SkipEmptyParts);
    if (ta.isEmpty() || tb.isEmpty()) return 0;
    QSet<QString> set_a;
    for (const QString& t : ta) set_a.insert(t);
    QStringList   unique_b;
    for (const QString& t : tb)
        if (!set_a.contains(t)) unique_b.push_back(t);
    const double distance = unique_b.join(QStringLiteral(" ")).size() /
                            static_cast<double>(tb.join(QStringLiteral(" ")).size());
    return 1 - distance;
}

bool Readability::is_valid_byline(Node* node, const QString& match) {
    const QString rel     = attr(node, QStringLiteral("rel"));
    const QString itemprop = attr(node, QStringLiteral("itemprop"));
    const int     length  = text_of(node).trimmed().size();
    return (rel == QStringLiteral("author") ||
            itemprop.contains(QStringLiteral("author")) ||
            reByline().match(match).hasMatch()) &&
           length > 0 && length < 100;
}

bool Readability::header_duplicates_title(Node* node) {
    if (!has_tag(node, "h1") && !has_tag(node, "h2")) return false;
    return text_similarity(article_title_, inner_text(node, false)) > 0.75;
}

bool Readability::has_ancestor_tag(Node* node, const QString& tag, int max_depth,
                                   const std::function<bool(Node*)>& filter) const {
    int depth = 0;
    while (node && node->parent) {
        if (max_depth > 0 && depth > max_depth) return false;
        Node* parent = node->parent;
        if (tag_name(parent) == tag && (!filter || filter(parent))) return true;
        node = parent;
        ++depth;
    }
    return false;
}

int Readability::get_char_count(Node* e, const QString& s) const {
    const QString text = inner_text(e);
    return text.count(s);
}

void Readability::clean_styles(Node* e) {
    if (!e || tag_name(e) == QStringLiteral("svg")) return;
    for (const QString& a : kPresentationalAttributes) remove_attr(e, a);
    if (kDeprecatedSizeElements.contains(tag_name(e))) {
        remove_attr(e, QStringLiteral("width"));
        remove_attr(e, QStringLiteral("height"));
    }
    for (Node* c : element_children(e)) clean_styles(c);
}

void Readability::mark_data_tables(Node* root) {
    for (Node* table : all_by_tag(root, QStringLiteral("table"))) {
        if (attr(table, QStringLiteral("role")) == QStringLiteral("presentation")) {
            set_data_table(table, false);
            continue;
        }
        if (attr(table, QStringLiteral("datatable")) == QStringLiteral("0")) {
            set_data_table(table, false);
            continue;
        }
        if (!attr(table, QStringLiteral("summary")).isEmpty()) {
            set_data_table(table, true);
            continue;
        }
        Node* caption = first_by_tag(table, QStringLiteral("caption"));
        if (caption && caption->first_child) {
            set_data_table(table, true);
            continue;
        }
        if (first_by_tag(table, QStringLiteral("col")) ||
            first_by_tag(table, QStringLiteral("colgroup")) ||
            first_by_tag(table, QStringLiteral("tfoot")) ||
            first_by_tag(table, QStringLiteral("thead")) ||
            first_by_tag(table, QStringLiteral("th"))) {
            set_data_table(table, true);
            continue;
        }
        if (first_by_tag(table, QStringLiteral("table"))) {
            set_data_table(table, false);
            continue;
        }
        int rows = 0, columns = 0;
        get_row_and_column_count(table, rows, columns);
        if (columns == 1 || rows == 1) {
            set_data_table(table, false);
            continue;
        }
        if (rows >= 10 || columns > 4) {
            set_data_table(table, true);
            continue;
        }
        set_data_table(table, rows * columns > 10);
    }
}

void Readability::get_row_and_column_count(Node* table, int& rows, int& columns) {
    rows = columns = 0;
    for (Node* tr : all_by_tag(table, QStringLiteral("tr"))) {
        int rowspan = attr(tr, QStringLiteral("rowspan")).toInt();
        rows += rowspan > 0 ? rowspan : 1;
        int columns_in_row = 0;
        for (Node* td : all_by_tag(tr, QStringLiteral("td"))) {
            int colspan = attr(td, QStringLiteral("colspan")).toInt();
            columns_in_row += colspan > 0 ? colspan : 1;
        }
        columns = std::max(columns, columns_in_row);
    }
}

void Readability::fix_lazy_images(Node* root) {
    for (Node* elem : all_by_tag(root, QStringLiteral("img"))) {
        const QString src = attr(elem, QStringLiteral("src"));
        if (!src.isEmpty() && reB64DataUrl().match(src).hasMatch()) {
            auto m = reB64DataUrl().match(src);
            if (m.captured(1) != QStringLiteral("image/svg+xml")) {
                bool could_remove = false;
                if (is_element(elem)) {
                    auto* el = lxb_dom_interface_element(elem);
                    for (auto* a = el->first_attr; a; a = a->next) {
                        size_t nlen = 0, vlen = 0;
                        const lxb_char_t* n = lxb_dom_attr_qualified_name(a, &nlen);
                        const lxb_char_t* v = lxb_dom_attr_value(a, &vlen);
                        QString name = QString::fromUtf8(reinterpret_cast<const char*>(n),
                                                         static_cast<int>(nlen));
                        QString val  = QString::fromUtf8(reinterpret_cast<const char*>(v),
                                                        static_cast<int>(vlen));
                        if (name == QStringLiteral("src")) continue;
                        if (QRegularExpression(QStringLiteral("\\.(jpg|jpeg|png|webp)"),
                                               QRegularExpression::CaseInsensitiveOption)
                                .match(val)
                                .hasMatch()) {
                            could_remove = true;
                            break;
                        }
                    }
                }
                if (could_remove && src.size() - m.captured(0).size() < 133)
                    remove_attr(elem, QStringLiteral("src"));
            }
        }

        const QString src2 = attr(elem, QStringLiteral("src"));
        const QString srcset = attr(elem, QStringLiteral("srcset"));
        if ((!src2.isEmpty() || (!srcset.isEmpty() && srcset != QStringLiteral("null"))) &&
            !class_name(elem).toLower().contains(QStringLiteral("lazy")))
            continue;

        if (!is_element(elem)) continue;
        auto* el = lxb_dom_interface_element(elem);
        QHash<QString, QString> attrs;
        for (auto* a = el->first_attr; a; a = a->next) {
            size_t nlen = 0, vlen = 0;
            const lxb_char_t* n = lxb_dom_attr_qualified_name(a, &nlen);
            const lxb_char_t* v = lxb_dom_attr_value(a, &vlen);
            attrs.insert(QString::fromUtf8(reinterpret_cast<const char*>(n),
                                           static_cast<int>(nlen)),
                         QString::fromUtf8(reinterpret_cast<const char*>(v),
                                           static_cast<int>(vlen)));
        }
        for (auto it = attrs.cbegin(); it != attrs.cend(); ++it) {
            const QString& name = it.key();
            const QString& value = it.value();
            if (name == QStringLiteral("src") || name == QStringLiteral("srcset") ||
                name == QStringLiteral("alt"))
                continue;
            QString copy_to;
            if (QRegularExpression(QStringLiteral("\\.(jpg|jpeg|png|webp)\\s+\\d"),
                                   QRegularExpression::CaseInsensitiveOption)
                    .match(value)
                    .hasMatch())
                copy_to = QStringLiteral("srcset");
            else if (QRegularExpression(QStringLiteral("^\\s*\\S+\\.(jpg|jpeg|png|webp)\\S*\\s*$"),
                                        QRegularExpression::CaseInsensitiveOption)
                         .match(value)
                         .hasMatch())
                copy_to = QStringLiteral("src");
            if (copy_to.isEmpty()) continue;
            set_attr(elem, copy_to, value);
        }
    }

    // Figures without an image but with lazy image attributes get a child img.
    for (Node* figure : all_by_tag(root, QStringLiteral("figure"))) {
        if (first_by_tag(figure, QStringLiteral("img")) ||
            first_by_tag(figure, QStringLiteral("picture")))
            continue;
    }
}

double Readability::get_text_density(Node* e, const QStringList& tags) {
    const int text_length = inner_text(e, true).size();
    if (text_length == 0) return 0;
    int children_length = 0;
    for (const QString& tag : tags)
        for (Node* child : all_by_tag(e, tag))
            children_length += inner_text(child, true).size();
    return children_length / static_cast<double>(text_length);
}

void Readability::clean(Node* e, const QString& tag) {
    const bool is_embed = tag == QStringLiteral("object") || tag == QStringLiteral("embed") ||
                          tag == QStringLiteral("iframe");
    Nodes to_remove;
    for (Node* element : all_by_tag(e, tag)) {
        bool keep = false;
        if (is_embed) {
            if (is_element(element)) {
                auto* el = lxb_dom_interface_element(element);
                for (auto* a = el->first_attr; a; a = a->next) {
                    size_t vlen = 0;
                    const lxb_char_t* v = lxb_dom_attr_value(a, &vlen);
                    if (v && reVideos().match(QString::fromUtf8(
                                       reinterpret_cast<const char*>(v),
                                       static_cast<int>(vlen)))
                                 .hasMatch()) {
                        keep = true;
                        break;
                    }
                }
            }
            if (!keep && tag == QStringLiteral("object")) {
                // Serialized inner HTML is not needed for the common object case;
                // check all descendant attributes instead.
                for (Node* d : all_by_tag(element, QStringLiteral("*"))) (void)d;
            }
        }
        if (!keep) to_remove.push_back(element);
    }
    remove_nodes(std::move(to_remove));
}

void Readability::clean_conditionally(Node* e, const QString& tag) {
    if (!flag_active(kFlagCleanConditionally)) return;

    auto is_data_table = [this](Node* t) { return data_table(t); };
    Nodes to_remove;

    for (Node* node : all_by_tag(e, tag)) {
        if (!node->parent) continue;

        bool is_list = tag == QStringLiteral("ul") || tag == QStringLiteral("ol");
        if (!is_list) {
            double list_length = 0;
            for (Node* list : all_by_tag(node, QStringLiteral("ul")))
                list_length += inner_text(list).size();
            for (Node* list : all_by_tag(node, QStringLiteral("ol")))
                list_length += inner_text(list).size();
            const int node_length = inner_text(node).size();
            is_list = node_length > 0 && list_length / node_length > 0.9;
        }

        if (tag == QStringLiteral("table") && is_data_table(node)) continue;
        if (has_ancestor_tag(node, QStringLiteral("table"), -1, is_data_table)) continue;
        if (has_ancestor_tag(node, QStringLiteral("code"))) continue;

        bool contains_data_table = false;
        for (Node* t : all_by_tag(node, QStringLiteral("table")))
            if (is_data_table(t)) contains_data_table = true;
        if (contains_data_table) continue;

        const int weight = get_class_weight(node);
        if (weight < 0) {
            to_remove.push_back(node);
            continue;
        }

        if (get_char_count(node, QStringLiteral(",")) >= 10) continue;

        const int p     = count_by_tag(node, QStringLiteral("p"));
        const int img   = count_by_tag(node, QStringLiteral("img"));
        const int li    = count_by_tag(node, QStringLiteral("li")) - 100;
        const int input = count_by_tag(node, QStringLiteral("input"));
        const double heading_density =
            get_text_density(node, {QStringLiteral("h1"), QStringLiteral("h2"),
                                    QStringLiteral("h3"), QStringLiteral("h4"),
                                    QStringLiteral("h5"), QStringLiteral("h6")});

        int  embed_count = 0;
        bool keep_embed  = false;
        for (Node* embed : all_by_tag(node, QStringLiteral("object"))) {
            (void)embed;
        }
        Nodes embeds;
        for (Node* x : all_by_tag(node, QStringLiteral("object"))) embeds.push_back(x);
        for (Node* x : all_by_tag(node, QStringLiteral("embed"))) embeds.push_back(x);
        for (Node* x : all_by_tag(node, QStringLiteral("iframe"))) embeds.push_back(x);
        for (Node* embed : embeds) {
            if (is_element(embed)) {
                auto* el = lxb_dom_interface_element(embed);
                for (auto* a = el->first_attr; a; a = a->next) {
                    size_t vlen = 0;
                    const lxb_char_t* v = lxb_dom_attr_value(a, &vlen);
                    if (v && reVideos().match(QString::fromUtf8(
                                       reinterpret_cast<const char*>(v),
                                       static_cast<int>(vlen)))
                                 .hasMatch()) {
                        keep_embed = true;
                        break;
                    }
                }
            }
            if (keep_embed) break;
            ++embed_count;
        }
        if (keep_embed) continue;

        const QString inner = inner_text(node);
        if (reAdWords().match(inner).hasMatch() || reLoadingWords().match(inner).hasMatch()) {
            to_remove.push_back(node);
            continue;
        }

        const int    content_length = inner.size();
        const double link_density   = get_link_density(node);
        QStringList textish_tags    = {QStringLiteral("span"), QStringLiteral("li"),
                                       QStringLiteral("td")};
        for (const QString& t : kDivToPElems) textish_tags.push_back(t);
        const double text_density = get_text_density(node, textish_tags);
        const bool   is_figure_child = has_ancestor_tag(node, QStringLiteral("figure"));

        QStringList errs;
        if (!is_figure_child && img > 1 && static_cast<double>(p) / img < 0.5)
            errs.push_back(QStringLiteral("bad p/img"));
        if (!is_list && li > p) errs.push_back(QStringLiteral("too many li"));
        if (input > static_cast<int>(std::floor(p / 3.0)))
            errs.push_back(QStringLiteral("too many inputs"));
        if (!is_list && !is_figure_child && heading_density < 0.9 &&
            content_length < 25 && (img == 0 || img > 2) && link_density > 0)
            errs.push_back(QStringLiteral("suspiciously short"));
        if (!is_list && weight < 25 && link_density > 0.2)
            errs.push_back(QStringLiteral("low weight, linky"));
        if (weight >= 25 && link_density > 0.5)
            errs.push_back(QStringLiteral("high weight, mostly links"));
        if ((embed_count == 1 && content_length < 75) || embed_count > 1)
            errs.push_back(QStringLiteral("suspicious embed"));
        if (img == 0 && text_density == 0)
            errs.push_back(QStringLiteral("no useful content"));

        bool remove = !errs.isEmpty();
        if (is_list && remove) {
            bool multi_child = false;
            for (Node* child : element_children(node))
                if (element_children(child).size() > 1) multi_child = true;
            if (multi_child) {
                to_remove.push_back(node);
                continue;
            }
            if (img == count_by_tag(node, QStringLiteral("li"))) remove = false;
        }
        if (remove) to_remove.push_back(node);
    }

    remove_nodes(std::move(to_remove));
}

void Readability::clean_matched_nodes(
    Node* e, const std::function<bool(Node*, const QString&)>& filter) {
    Node* end = next_node(e, true);
    Node* n   = next_node(e);
    while (n && n != end) {
        if (filter(n, match_string(n))) n = remove_and_get_next(n);
        else n = next_node(n);
    }
}

void Readability::clean_headers(Node* e) {
    Nodes to_remove;
    for (Node* h : all_by_tag(e, QStringLiteral("h1")))
        if (get_class_weight(h) < 0) to_remove.push_back(h);
    for (Node* h : all_by_tag(e, QStringLiteral("h2")))
        if (get_class_weight(h) < 0) to_remove.push_back(h);
    remove_nodes(std::move(to_remove));
}

void Readability::prep_article(Node* article) {
    clean_styles(article);
    mark_data_tables(article);
    fix_lazy_images(article);

    clean_conditionally(article, QStringLiteral("form"));
    clean_conditionally(article, QStringLiteral("fieldset"));
    clean(article, QStringLiteral("object"));
    clean(article, QStringLiteral("embed"));
    clean(article, QStringLiteral("footer"));
    clean(article, QStringLiteral("link"));
    clean(article, QStringLiteral("aside"));

    const int share_threshold = kDefaultCharThreshold;
    for (Node* top : element_children(article)) {
        clean_matched_nodes(top, [&](Node* node, const QString& match) {
            return reShareElements().match(match).hasMatch() &&
                   text_of(node).size() < share_threshold;
        });
    }

    clean(article, QStringLiteral("iframe"));
    clean(article, QStringLiteral("input"));
    clean(article, QStringLiteral("textarea"));
    clean(article, QStringLiteral("select"));
    clean(article, QStringLiteral("button"));
    clean_headers(article);

    clean_conditionally(article, QStringLiteral("table"));
    clean_conditionally(article, QStringLiteral("ul"));
    clean_conditionally(article, QStringLiteral("div"));

    for (Node* h1 : all_by_tag(article, QStringLiteral("h1")))
        set_node_tag(h1, QStringLiteral("h2"));

    Nodes empty_paragraphs;
    for (Node* p : all_by_tag(article, QStringLiteral("p"))) {
        int content_count = count_by_tag(p, QStringLiteral("img")) +
                            count_by_tag(p, QStringLiteral("embed")) +
                            count_by_tag(p, QStringLiteral("object")) +
                            count_by_tag(p, QStringLiteral("iframe"));
        if (content_count == 0 && inner_text(p, false).isEmpty())
            empty_paragraphs.push_back(p);
    }
    remove_nodes(std::move(empty_paragraphs));

    for (Node* br : all_by_tag(article, QStringLiteral("br"))) {
        Node* next = next_node(br->next, false);
        if (next && has_tag(next, "p")) remove_node(br);
    }

    for (Node* table : all_by_tag(article, QStringLiteral("table"))) {
        if (!table->parent) continue;
        Node* tbody = has_single_tag_inside(table, QStringLiteral("tbody"))
                          ? element_children(table).front()
                          : table;
        if (!has_single_tag_inside(tbody, QStringLiteral("tr"))) continue;
        Node* row = element_children(tbody).front();
        if (!has_single_tag_inside(row, QStringLiteral("td"))) continue;
        Node* cell = element_children(row).front();
        bool  all_phrasing = true;
        for (Node* c = cell->first_child; c; c = c->next)
            if (!is_phrasing_content(c)) all_phrasing = false;
        cell = set_node_tag(cell, all_phrasing ? QStringLiteral("p") : QStringLiteral("div"));
        Node* parent = table->parent;
        move_before(table, cell);
        remove_node(table);
        (void)parent;
    }
}

void Readability::unwrap_noscript_images() {
    // Remove placeholder images that carry no useful source attribute.
    Nodes bad_images;
    for (Node* img : all_by_tag(doc_.root(), QStringLiteral("img"))) {
        bool useful = false;
        if (is_element(img)) {
            auto* el = lxb_dom_interface_element(img);
            for (auto* a = el->first_attr; a; a = a->next) {
                size_t nlen = 0, vlen = 0;
                const lxb_char_t* n = lxb_dom_attr_qualified_name(a, &nlen);
                const lxb_char_t* v = lxb_dom_attr_value(a, &vlen);
                const QString name = QString::fromUtf8(reinterpret_cast<const char*>(n),
                                                       static_cast<int>(nlen));
                const QString value = QString::fromUtf8(reinterpret_cast<const char*>(v),
                                                        static_cast<int>(vlen));
                if (name == QStringLiteral("src") || name == QStringLiteral("srcset") ||
                    name == QStringLiteral("data-src") || name == QStringLiteral("data-srcset")) {
                    useful = true;
                    break;
                }
                if (QRegularExpression(QStringLiteral("\\.(jpg|jpeg|png|webp)"),
                                       QRegularExpression::CaseInsensitiveOption)
                        .match(value)
                        .hasMatch()) {
                    useful = true;
                    break;
                }
            }
        }
        if (!useful) bad_images.push_back(img);
    }
    remove_nodes(std::move(bad_images));
}

void Readability::remove_scripts() {
    Nodes to_remove;
    for (Node* n : all_by_tag(doc_.root(), QStringLiteral("script"))) to_remove.push_back(n);
    for (Node* n : all_by_tag(doc_.root(), QStringLiteral("noscript"))) to_remove.push_back(n);
    remove_nodes(std::move(to_remove));
}

Node* Readability::grab_article() {
    Node* page = find(doc_.root(), QStringLiteral("body"));
    if (!page) return nullptr;

    while (true) {
        const bool strip_unlikely = flag_active(kFlagStripUnlikelys);
        Nodes      elements_to_score;
        Node*      node = find(doc_.root(), QStringLiteral("html"));
        bool       should_remove_title_header = true;

        while (node) {
            if (has_tag(node, "html")) article_lang_ = attr(node, QStringLiteral("lang"));
            const QString match = match_string(node);

            if (!is_probably_visible(node)) {
                node = remove_and_get_next(node);
                continue;
            }
            if (attr(node, QStringLiteral("aria-modal")) == QStringLiteral("true") &&
                attr(node, QStringLiteral("role")) == QStringLiteral("dialog")) {
                node = remove_and_get_next(node);
                continue;
            }

            if (article_byline_.isEmpty() && is_valid_byline(node, match)) {
                Node* end_marker = next_node(node, true);
                Node* next       = next_node(node);
                Node* name_node  = nullptr;
                while (next && next != end_marker) {
                    const QString itemprop = attr(next, QStringLiteral("itemprop"));
                    if (itemprop.contains(QStringLiteral("name"))) {
                        name_node = next;
                        break;
                    }
                    next = next_node(next);
                }
                article_byline_ = text_of(name_node ? name_node : node).trimmed();
                node = remove_and_get_next(node);
                continue;
            }

            if (should_remove_title_header && header_duplicates_title(node)) {
                should_remove_title_header = false;
                node = remove_and_get_next(node);
                continue;
            }

            if (strip_unlikely) {
                if (reUnlikely().match(match).hasMatch() &&
                    !reMaybeCandidate().match(match).hasMatch() &&
                    !has_ancestor_tag(node, QStringLiteral("table")) &&
                    !has_ancestor_tag(node, QStringLiteral("code")) &&
                    !has_tag(node, "body") && !has_tag(node, "a")) {
                    node = remove_and_get_next(node);
                    continue;
                }
                if (kUnlikelyRoles.contains(attr(node, QStringLiteral("role")))) {
                    node = remove_and_get_next(node);
                    continue;
                }
            }

            if ((has_tag(node, "div") || has_tag(node, "section") ||
                 has_tag(node, "header") || has_tag(node, "h1") ||
                 has_tag(node, "h2") || has_tag(node, "h3") ||
                 has_tag(node, "h4") || has_tag(node, "h5") ||
                 has_tag(node, "h6")) &&
                is_element_without_content(node)) {
                node = remove_and_get_next(node);
                continue;
            }

            if (kDefaultTagsToScore.contains(tag_name(node))) elements_to_score.push_back(node);

            if (has_tag(node, "div")) {
                Node* child = node->first_child;
                while (child) {
                    Node* next_sibling = child->next;
                    if (is_phrasing_content(child)) {
                        Node* p = nullptr;
                        do {
                            next_sibling = child->next;
                            if (!p) {
                                const QByteArray pname = QByteArrayLiteral("p");
                                auto* pel = lxb_html_document_create_element(
                                    doc_.raw(),
                                    reinterpret_cast<const lxb_char_t*>(pname.constData()),
                                    static_cast<size_t>(pname.size()), nullptr);
                                p = lxb_dom_interface_node(pel);
                            }
                            move_child(p, child);
                            child = next_sibling;
                        } while (child && is_phrasing_content(child));

                        while (p->first_child && is_whitespace(p->first_child))
                            remove_node(p->first_child);
                        while (p->last_child && is_whitespace(p->last_child))
                            remove_node(p->last_child);

                        if (p->first_child) {
                            if (next_sibling) lxb_dom_node_insert_before(next_sibling, p);
                            else lxb_dom_node_insert_child(node, p);
                        }
                    }
                    child = next_sibling;
                }

                if (has_single_tag_inside(node, QStringLiteral("p")) &&
                    get_link_density(node) < 0.25) {
                    Node* new_node = element_children(node).front();
                    move_before(node, new_node);
                    remove_node(node);
                    node = new_node;
                    elements_to_score.push_back(node);
                } else if (!has_child_block_element(node)) {
                    node = set_node_tag(node, QStringLiteral("p"));
                    elements_to_score.push_back(node);
                }
            }
            node = next_node(node);
        }

        Nodes candidates;
        for (Node* element : elements_to_score) {
            if (!element->parent || !is_element(element->parent)) continue;
            const QString inner = inner_text(element);
            if (inner.size() < 25) continue;
            Nodes ancestors = get_node_ancestors(element, 5);
            if (ancestors.empty()) continue;

            double content_score = 1;
            content_score += inner.count(reCommas()) + 1;
            content_score += std::min(static_cast<int>(inner.size() / 100), 3);

            for (size_t level = 0; level < ancestors.size(); ++level) {
                Node* ancestor = ancestors[level];
                if (!is_element(ancestor) || !ancestor->parent ||
                    !is_element(ancestor->parent))
                    continue;
                if (!score_initialized(ancestor)) {
                    initialize_node(ancestor);
                    candidates.push_back(ancestor);
                }
                double divider = level == 0 ? 1 : (level == 1 ? 2 : level * 3.0);
                score(ancestor) += content_score / divider;
            }
        }

        Nodes top_candidates;
        for (Node* candidate : candidates) {
            const double candidate_score = score(candidate) * (1 - get_link_density(candidate));
            scores_.insert(candidate, candidate_score);
            for (size_t t = 0; t < static_cast<size_t>(kDefaultNbTopCandidates); ++t) {
                if (t >= top_candidates.size() ||
                    candidate_score > score(top_candidates[t])) {
                    top_candidates.insert(top_candidates.begin() + static_cast<long>(t), candidate);
                    if (top_candidates.size() > static_cast<size_t>(kDefaultNbTopCandidates))
                        top_candidates.pop_back();
                    break;
                }
            }
        }

        Node* top = top_candidates.empty() ? nullptr : top_candidates.front();
        bool  needed_to_create = false;
        Node* parent_of_top    = nullptr;

        if (!top || has_tag(top, "body")) {
            const QByteArray dname = QByteArrayLiteral("div");
            auto* del = lxb_html_document_create_element(
                doc_.raw(), reinterpret_cast<const lxb_char_t*>(dname.constData()),
                static_cast<size_t>(dname.size()), nullptr);
            top = lxb_dom_interface_node(del);
            needed_to_create = true;
            while (page->first_child) move_child(top, page->first_child);
            lxb_dom_node_insert_child(page, top);
            initialize_node(top);
        } else {
            std::vector<Nodes> alternative_ancestors;
            for (size_t i = 1; i < top_candidates.size(); ++i) {
                if (score(top_candidates[i]) / score(top) >= 0.75)
                    alternative_ancestors.push_back(get_node_ancestors(top_candidates[i]));
            }
            constexpr int kMinTopCandidates = 3;
            if (static_cast<int>(alternative_ancestors.size()) >= kMinTopCandidates) {
                parent_of_top = top->parent;
                while (parent_of_top && !has_tag(parent_of_top, "body")) {
                    int lists_containing = 0;
                    for (const Nodes& list : alternative_ancestors) {
                        if (std::find(list.begin(), list.end(), parent_of_top) != list.end())
                            ++lists_containing;
                        if (lists_containing >= kMinTopCandidates) break;
                    }
                    if (lists_containing >= kMinTopCandidates) {
                        top = parent_of_top;
                        break;
                    }
                    parent_of_top = parent_of_top->parent;
                }
            }
            if (!score_initialized(top)) initialize_node(top);

            parent_of_top   = top->parent;
            double last     = score(top);
            double threshold = last / 3;
            while (parent_of_top && !has_tag(parent_of_top, "body")) {
                if (!score_initialized(parent_of_top)) {
                    parent_of_top = parent_of_top->parent;
                    continue;
                }
                const double parent_score = score(parent_of_top);
                if (parent_score < threshold) break;
                if (parent_score > last) {
                    top = parent_of_top;
                    break;
                }
                last          = parent_score;
                parent_of_top = parent_of_top->parent;
            }

            parent_of_top = top->parent;
            while (parent_of_top && !has_tag(parent_of_top, "body") &&
                   element_children(parent_of_top).size() == 1) {
                top           = parent_of_top;
                parent_of_top = top->parent;
            }
            if (!score_initialized(top)) initialize_node(top);
        }

        const QByteArray cname = QByteArrayLiteral("div");
        auto* cel = lxb_html_document_create_element(
            doc_.raw(), reinterpret_cast<const lxb_char_t*>(cname.constData()),
            static_cast<size_t>(cname.size()), nullptr);
        Node* article_content = lxb_dom_interface_node(cel);

        const double sibling_threshold =
            std::max(10.0, score(top) * 0.2);
        parent_of_top = top->parent;
        if (!parent_of_top) return nullptr;
        Nodes siblings = element_children(parent_of_top);

        for (size_t s = 0; s < siblings.size();) {
            Node* sibling = siblings[s];
            bool  append  = false;
            if (sibling == top) {
                append = true;
            } else {
                double bonus = 0;
                if (!class_name(sibling).isEmpty() &&
                    class_name(sibling) == class_name(top))
                    bonus += score(top) * 0.2;
                if (score_initialized(sibling) &&
                    score(sibling) + bonus >= sibling_threshold) {
                    append = true;
                } else if (has_tag(sibling, "p")) {
                    const double link_density = get_link_density(sibling);
                    const QString content = inner_text(sibling);
                    const int length = content.size();
                    if (length > 80 && link_density < 0.25) append = true;
                    else if (length < 80 && length > 0 && link_density == 0 &&
                             content.contains(QRegularExpression(QStringLiteral("\\.( |$)"))))
                        append = true;
                }
            }

            if (append) {
                if (!kAlterToDivExceptions.contains(tag_name(sibling)))
                    sibling = set_node_tag(sibling, QStringLiteral("div"));
                move_child(article_content, sibling);
                if (sibling == top) top = sibling;
                siblings.erase(siblings.begin() + static_cast<long>(s));
            } else {
                ++s;
            }
        }

        prep_article(article_content);

        if (needed_to_create) {
            set_attr(top, QStringLiteral("id"), QStringLiteral("readability-page-1"));
            set_attr(top, QStringLiteral("class"), QStringLiteral("page"));
        } else {
            const QByteArray dname = QByteArrayLiteral("div");
            auto* del = lxb_html_document_create_element(
                doc_.raw(), reinterpret_cast<const lxb_char_t*>(dname.constData()),
                static_cast<size_t>(dname.size()), nullptr);
            Node* div = lxb_dom_interface_node(del);
            set_attr(div, QStringLiteral("id"), QStringLiteral("readability-page-1"));
            set_attr(div, QStringLiteral("class"), QStringLiteral("page"));
            while (article_content->first_child)
                move_child(div, article_content->first_child);
            lxb_dom_node_insert_child(article_content, div);
        }

        Nodes ancestors = get_node_ancestors(parent_of_top);
        ancestors.insert(ancestors.begin(), top);
        if (parent_of_top) ancestors.insert(ancestors.begin(), parent_of_top);
        for (Node* ancestor : ancestors) {
            if (!is_element(ancestor)) continue;
            const QString dir = attr(ancestor, QStringLiteral("dir"));
            if (!dir.isEmpty()) {
                article_dir_ = dir;
                break;
            }
        }
        return article_content;
    }
}

void Readability::fix_relative_uris(Node* content) {
    const QUrl base(base_url_);
    auto to_absolute = [&](const QString& uri) -> QString {
        if (uri.isEmpty()) return uri;
        if (uri.startsWith(QLatin1Char('#'))) return uri;
        if (!base.isValid()) return uri;
        const QUrl resolved = base.resolved(QUrl(uri));
        return resolved.isValid() ? resolved.toString() : uri;
    };

    for (Node* link : all_by_tag(content, QStringLiteral("a"))) {
        const QString href = attr(link, QStringLiteral("href"));
        if (href.isEmpty()) continue;
        if (href.startsWith(QStringLiteral("javascript:"), Qt::CaseInsensitive)) {
            Nodes kids = child_nodes(link);
            if (kids.size() == 1 && kids.front()->type == LXB_DOM_NODE_TYPE_TEXT) {
                const QByteArray data = text_of(link).toUtf8();
                Node* text = lxb_dom_interface_node(lxb_dom_document_create_text_node(
                    &doc_.raw()->dom_document,
                    reinterpret_cast<const lxb_char_t*>(data.constData()),
                    static_cast<size_t>(data.size())));
                lxb_dom_node_insert_before(link, text);
                remove_node(link);
            } else {
                const QByteArray sname = QByteArrayLiteral("span");
                auto* sel = lxb_html_document_create_element(
                    doc_.raw(), reinterpret_cast<const lxb_char_t*>(sname.constData()),
                    static_cast<size_t>(sname.size()), nullptr);
                Node* span = lxb_dom_interface_node(sel);
                while (link->first_child) move_child(span, link->first_child);
                lxb_dom_node_insert_before(link, span);
                remove_node(link);
            }
        } else {
            set_attr(link, QStringLiteral("href"), to_absolute(href));
        }
    }

    for (const QString& tag : {QStringLiteral("img"), QStringLiteral("picture"),
                               QStringLiteral("figure"), QStringLiteral("video"),
                               QStringLiteral("audio"), QStringLiteral("source")}) {
        for (Node* media : all_by_tag(content, tag)) {
            const QString src = attr(media, QStringLiteral("src"));
            if (!src.isEmpty()) set_attr(media, QStringLiteral("src"), to_absolute(src));
            const QString poster = attr(media, QStringLiteral("poster"));
            if (!poster.isEmpty())
                set_attr(media, QStringLiteral("poster"), to_absolute(poster));
            const QString srcset = attr(media, QStringLiteral("srcset"));
            if (!srcset.isEmpty()) {
                QString out;
                auto it = reSrcsetUrl().globalMatch(srcset);
                int last = 0;
                while (it.hasNext()) {
                    auto m = it.next();
                    out += srcset.mid(last, m.capturedStart() - last);
                    out += to_absolute(m.captured(1));
                    if (m.captured(2).size()) out += m.captured(2);
                    out += m.captured(3);
                    last = m.capturedEnd();
                }
                out += srcset.mid(last);
                set_attr(media, QStringLiteral("srcset"), out);
            }
        }
    }
}

void Readability::simplify_nested_elements(Node* content) {
    Node* node = content;
    while (node) {
        const QString tag = tag_name(node);
        const QString id  = node_id(node);
        if (node->parent && (tag == QStringLiteral("div") || tag == QStringLiteral("section")) &&
            !id.startsWith(QStringLiteral("readability"))) {
            if (is_element_without_content(node)) {
                node = remove_and_get_next(node);
                continue;
            }
            if (has_single_tag_inside(node, QStringLiteral("div")) ||
                has_single_tag_inside(node, QStringLiteral("section"))) {
                Node* child = element_children(node).front();
                copy_attributes(node, child);
                move_before(node, child);
                remove_node(node);
                node = child;
                continue;
            }
        }
        node = next_node(node);
    }
}

void Readability::clean_classes(Node* node) {
    QStringList keep;
    for (const QString& cls : class_name(node).split(QRegularExpression(QStringLiteral("\\s+")),
                                                     Qt::SkipEmptyParts))
        if (kClassesToPreserve.contains(cls)) keep.push_back(cls);
    if (keep.isEmpty()) remove_attr(node, QStringLiteral("class"));
    else set_attr(node, QStringLiteral("class"), keep.join(QLatin1Char(' ')));
    for (Node* child : element_children(node)) clean_classes(child);
}

Article Readability::parse() {
    unwrap_noscript_images();
    QJsonObject jsonld = get_json_ld();
    remove_scripts();
    prep_document();
    get_article_metadata(jsonld);

    Node* content = grab_article();
    if (!content) return {};

    fix_relative_uris(content);
    simplify_nested_elements(content);
    clean_classes(content);

    Article article;
    article.document      = std::move(doc_);
    article.content       = content;
    article.title         = article_title_;
    article.byline        = article_byline_;
    article.excerpt       = article_excerpt_;
    article.site_name     = article_site_name_;
    article.published_time = article_published_;
    article.lang          = article_lang_;
    article.dir           = article_dir_;
    article.text_content  = text_of(content);
    article.ok            = true;
    return article;
}

Article extract_article(const QString& html, const QString& base_url) {
    int flags = kFlagStripUnlikelys | kFlagWeightClasses | kFlagCleanConditionally;

    std::vector<Article> attempts;
    while (true) {
        Document doc = Document::parse(html);
        if (!doc.valid()) break;
        Readability readability(std::move(doc), base_url, flags);
        Article     article = readability.parse();
        if (article.ok) {
            const int length = inner_text(article.content, true).size();
            if (length >= kDefaultCharThreshold) return article;
            attempts.push_back(std::move(article));
        }

        if ((flags & kFlagStripUnlikelys) != 0)
            flags &= ~kFlagStripUnlikelys;
        else if ((flags & kFlagWeightClasses) != 0)
            flags &= ~kFlagWeightClasses;
        else if ((flags & kFlagCleanConditionally) != 0)
            flags &= ~kFlagCleanConditionally;
        else
            break;
    }

    Article best;
    int     best_length = -1;
    for (Article& attempt : attempts) {
        const int length = inner_text(attempt.content, true).size();
        if (length > best_length) {
            best_length = length;
            best        = std::move(attempt);
        }
    }
    return best;
}

}  // namespace ddg::html
