// SPDX-License-Identifier: AGPL-3.0-only

#include "utils.hpp"

#include <QDebug>
#include <QString>
#include <QUrl>

#include <cstdio>


namespace ddg
{

  QString normalize_cache_url(const QString& str)
  {
    QUrl::FormattingOptions flags = QUrl::RemoveFragment | QUrl::NormalizePathSegments;
    QUrl url(str);
    if (url.scheme().isEmpty())
      url = QUrl::fromUserInput(str), url.setScheme("https");
    if(url.query().isEmpty())
      flags |= QUrl::RemoveQuery;
    if((url.scheme() == "http" && url.port(80) == 80) ||
        (url.scheme() == "https" && url.port(443) == 443))
      flags |= QUrl::RemovePort;
    return url.toString(flags);
  }
}

namespace {

int g_failures = 0;

void check_normalized(const char* input, const char* expected) {
    const QString actual = ddg::normalize_cache_url(QString::fromUtf8(input));
    if (actual != QString::fromUtf8(expected)) {
       qInfo().noquote() << QString("FAIL: normalize_cache_url(%1)\n  expected: %2\n  actual:   %3\n")
                  .arg(input).arg(expected).arg(actual);
        ++g_failures;
    }
}

}  // namespace

int main() {
    check_normalized("HTTP://Example.COM/Path/../A?b=1#frag",
                     "http://example.com/A?b=1");
    check_normalized("https://example.com:443/a/b/",
                     "https://example.com/a/b/");
    check_normalized("http://example.com:80", "http://example.com");
    check_normalized("example.com", "https://example.com");
    check_normalized("example.com/foo", "https://example.com/foo");
    check_normalized("https://EXAMPLE.com/foo/./bar/../baz",
                     "https://example.com/foo//baz");
    check_normalized("https://example.com/a%20b?q=a%20b",
                     "https://example.com/a b?q=a b");
    check_normalized("https://example.com/a?b=1&c=2#frag",
                     "https://example.com/a?b=1&c=2");
    check_normalized("https://example.com", "https://example.com");
    check_normalized("https://example.com/", "https://example.com/");
    check_normalized("https://example.com:8443/x",
                     "https://example.com:8443/x");
    check_normalized("//example.com/x", "https://example.com/x");
    check_normalized("https://example.com/a//b", "https://example.com/a//b");
    check_normalized("https://example.com/?", "https://example.com/");
    check_normalized("https://example.com/?a=", "https://example.com/?a=");

    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}
