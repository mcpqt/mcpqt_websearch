// SPDX-License-Identifier: Apache-2.0
//
// End-to-end HTML -> markdown regression test for a saved DuckDuckGo lite
// results page. No network access is used.

#include "html_extract.hpp"

#include <QFile>
#include <QString>

#include <cstdio>
#include <string>
#include <string_view>

namespace {

int g_failures = 0;

void check(bool condition, std::string_view message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %.*s\n", static_cast<int>(message.size()),
                     message.data());
        ++g_failures;
    }
}

void expect_contains(const std::string& text, std::string_view needle) {
    if (text.find(needle) == std::string::npos) {
        std::fprintf(stderr, "FAIL: markdown is missing '%.*s'\n",
                     static_cast<int>(needle.size()), needle.data());
        ++g_failures;
    }
}

void expect_not_contains(const std::string& text, std::string_view needle) {
    if (text.find(needle) != std::string::npos) {
        std::fprintf(stderr, "FAIL: markdown still contains raw '%.*s'\n",
                     static_cast<int>(needle.size()), needle.data());
        ++g_failures;
    }
}

int count(const std::string& text, char needle) {
    int total = 0;
    for (char ch : text)
        if (ch == needle) ++total;
    return total;
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

    const std::string html = QString::fromUtf8(file.readAll()).toStdString();
    const std::string markdown = ddg::html::html_to_text(html, "markdown");

    check(!markdown.empty(), "markdown output is empty");
    check(markdown.find('\0') == std::string::npos, "markdown contains NUL bytes");
    check(markdown.find('\n') != std::string::npos,
          "markdown output has no newline characters");

    expect_contains(markdown, "SQL Tryit Editor v1.6 - W3Schools");
    expect_contains(markdown, "SQL Test");
    expect_contains(markdown, "SQL Playground");
    expect_contains(markdown, "SQL Fiddle - Online SQL Compiler");
    expect_contains(markdown, "SQL Online Editor (Compiler) - W3Schools");
    expect_contains(markdown, "SQL Practice Online");
    expect_contains(markdown, "DB Fiddle - SQL Database Playground");
    expect_contains(markdown, "SQL Playground: Write and Execute SQL Queries Online");
    expect_contains(markdown, "QuerySandbox - SQL playground");
    expect_contains(markdown, "Learn SQL - Online SQL Terminal");

    expect_contains(markdown, "www.w3schools.com/sql/trysql.asp");
    expect_contains(markdown, "sqltest.net");
    expect_contains(markdown, "www.sqltutorial.org/playground/");
    expect_contains(markdown, "sqlfiddle.com");
    expect_contains(markdown, "www.w3schools.com/sql/sql_editor.asp");
    expect_contains(markdown, "www.sqlpractice.in/practice");
    expect_contains(markdown, "www.db-fiddle.com");
    expect_contains(markdown, "sqltest.online/en/playground/");
    expect_contains(markdown, "querysandbox.com");
    expect_contains(markdown, "www.sql-practice.com");

    expect_not_contains(markdown, "&nbsp;");
    expect_not_contains(markdown, "<div");
    expect_not_contains(markdown, "<table");
    expect_not_contains(markdown, "<tr");
    expect_not_contains(markdown, "<td");
    expect_not_contains(markdown, "<script");
    expect_not_contains(markdown, "<style");
    expect_not_contains(markdown, "<form");
    expect_not_contains(markdown, "<a href");

    check(count(markdown, '[') == count(markdown, ']'),
          "markdown square brackets are unbalanced");
    check(count(markdown, '(') == count(markdown, ')'),
          "markdown parentheses are unbalanced");

    if (g_failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("markdown conversion OK (%zu bytes)\n", markdown.size());
    return 0;
}
