// SPDX-License-Identifier: AGPL-3.0-only
//
// C++ port of Mozilla's Readability.js article extractor, backed by Lexbor.
// The algorithm and scoring constants follow Readability.js (Apache-2.0,
// Copyright (c) 2010 Arc90 Inc) so extracted content stays close to Firefox
// Reader View.

#pragma once

#include "html_document.hpp"

#include <QString>

#include <lexbor/dom/dom.h>

namespace ddg::html {

struct Article {
    Document        document;
    lxb_dom_node_t* content = nullptr;
    QString         title;
    QString         byline;
    QString         excerpt;
    QString         site_name;
    QString         published_time;
    QString         lang;
    QString         dir;
    QString         text_content;
    bool            ok = false;

    Article() = default;
    Article(Article&&) noexcept            = default;
    Article& operator=(Article&&) noexcept = default;
    Article(const Article&)                = delete;
    Article& operator=(const Article&)     = delete;
};

/// Extract the main article from an HTML document. `base_url` is used to make
/// relative links and media sources absolute (Readability's documentURI).
Article extract_article(const QString& html, const QString& base_url = QString());

}  // namespace ddg::html
