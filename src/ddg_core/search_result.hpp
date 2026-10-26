// SPDX-License-Identifier: AGPL-3.0-only
//
// server.py SearchResult.

#pragma once

#include <QString>

namespace ddg {

struct SearchResult {
    QString title;
    QString link;
    QString snippet;
    int     position = 0;
};

}  // namespace ddg
