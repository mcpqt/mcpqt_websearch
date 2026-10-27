// SPDX-License-Identifier: AGPL-3.0-only
//
// Regression test for the QXmlStreamReader-backed search result parser. This is
// the parser the `search` tool always uses.

#include "searcher.hpp"

#include <QFile>
#include <QString>
#include <QUrl>

#include <cstdio>

namespace ddg {

// page_parser_xml.cpp calls this; searcher.cpp is not linked into this test.
QString decode_uddg(const QString& link) {
    const QString prefix = "//duckduckgo.com/l/?uddg=";
    if (!link.startsWith(prefix)) return link;
    QString         enc = link.mid(prefix.size());
    const qsizetype amp = enc.indexOf(QLatin1Char('&'));
    if (amp != -1) enc.truncate(amp);
    return QUrl::fromPercentEncoding(enc.toUtf8());
}

}  // namespace ddg

namespace {

int g_failures = 0;

void check(bool condition, const QString& message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message.toUtf8().constData());
        ++g_failures;
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <saved-ddg-page.html>\n", argv[0]);
        return 2;
    }

    QFile file(QString::fromLocal8Bit(argv[1]));
    if (!file.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "FAIL: cannot open fixture %s\n", argv[1]);
        return 2;
    }

    const QString html = QString::fromUtf8(file.readAll());
    const auto    results = ddg::parse_lite_results(html, 10);

    check(!results.empty(), "XML page parser returned no results");
    check(results.size() >= 5, "XML page parser returned too few results");

    if (!results.empty()) {
        check(results.front().title.contains("SQL Tryit Editor"),
              "first result title");
        check(results.front().link.startsWith(
                  "https://www.w3schools.com/sql/trysql.asp"),
              "first result link was not decoded");
        check(!results.front().link.contains("uddg="),
              "first result link still contains the redirect wrapper");
        check(!results.front().snippet.isEmpty(), "first result snippet is empty");
        check(results.front().position == 1, "first result position");
    }

    const ddg::LiteNextPage next = ddg::extract_lite_next_page(html);
    check(next.s == "10", "next-page s parameter");
    check(next.vqd == "4-121700496490801137926632225534822938806",
          "next-page vqd parameter");

    if (g_failures != 0) {
        std::fprintf(stderr, "%d XML page parser check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("XML page parser OK (%lld results)\n",
                static_cast<long long>(results.size()));
    return 0;
}
