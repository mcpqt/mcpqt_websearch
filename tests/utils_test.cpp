// SPDX-License-Identifier: AGPL-3.0-only

#include "utils.hpp"

#include <QString>

#include <cstdio>

namespace {

int g_failures = 0;

void check_normalized(const char* input, const char* expected) {
    const QString actual = ddg::normalize_cache_url(QString::fromUtf8(input));
    if (actual != QString::fromUtf8(expected)) {
        std::fprintf(stderr,
                     "FAIL: normalize_cache_url(%s)\n  expected: %s\n  actual:   %s\n",
                     input,
                     expected,
                     actual.toUtf8().constData());
        ++g_failures;
    }
}

}  // namespace

int main() {
    check_normalized("HTTP://Example.COM/Path/../A?b=1#frag",
                     "http://example.com/Path/../A?b=1");
    check_normalized("https://example.com:443/a/b/",
                     "https://example.com/a/b/");
    check_normalized("http://example.com:80", "http://example.com/");
    check_normalized("example.com", "http://example.com");
    check_normalized("example.com/foo", "http://example.com/foo");
    check_normalized("https://EXAMPLE.com/foo/./bar/../baz",
                     "https://example.com/foo/./bar/../baz");
    check_normalized("https://example.com/a%20b?q=a%20b",
                     "https://example.com/a b?q=a%20b");
    check_normalized("https://example.com/a?b=1&c=2#frag",
                     "https://example.com/a?b=1&c=2");
    check_normalized("https://example.com", "https://example.com/");
    check_normalized("https://example.com/", "https://example.com/");
    check_normalized("https://example.com:8443/x",
                     "https://example.com:8443/x");
    check_normalized("//example.com/x", "http://example.com/x");
    check_normalized("https://example.com/a//b", "https://example.com/a//b");
    check_normalized("https://example.com/?", "https://example.com/");
    check_normalized("https://example.com/?a=", "https://example.com/?a=");

    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}
