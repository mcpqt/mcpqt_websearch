// SPDX-License-Identifier: AGPL-3.0-only

#include "utils.hpp"

#include "blocked_url_error.hpp"
#include "constants.hpp"

#include <QHostAddress>
#include <QHostInfo>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <algorithm>
#include <chrono>
#include <thread>

namespace ddg {

// ---------------------------------------------------------------------------
// Strings
// ---------------------------------------------------------------------------

QString to_lower(QString s) { return s.toLower(); }

bool starts_with_ci(const QString& haystack, const QString& needle) {
    return haystack.startsWith(needle, Qt::CaseInsensitive);
}

QString strip_slashes(QString s) {
    while (s.startsWith(QLatin1Char('/'))) s.remove(0, 1);
    while (s.endsWith(QLatin1Char('/'))) s.chop(1);
    return s;
}

bool contains(const QStringList& v, const QString& s) { return v.contains(s); }

QString collapse_whitespace(const QString& text) {
    QStringList chunks;
    for (const QString& raw_line : text.split(QLatin1Char('\n'))) {
        const QString line = raw_line.trimmed();
        for (const QString& raw_phrase : line.split("  ")) {
            const QString phrase = raw_phrase.trimmed();
            if (!phrase.isEmpty()) chunks.push_back(phrase);
        }
    }
    QString joined = chunks.join(QLatin1Char(' '));
    joined.replace(QRegularExpression("\\s+"), " ");
    return joined.trimmed();
}

// ---------------------------------------------------------------------------
// URLs
// ---------------------------------------------------------------------------

QString url_join(const QString& base, const QString& location) {
    return QUrl(base).resolved(QUrl(location)).toString();
}

QString url_hostname(const QString& url) {
    return QUrl(url).host().toLower();
}

QString normalize_cache_url(const QString& url) {
    const QUrl    u(url);
    const QString scheme =
        u.scheme().isEmpty() ? "http" : u.scheme().toLower();
    const QString host = u.host().toLower();
    const int     port = u.port(-1);

    QString netloc = host;
    if (port > 0 && !((scheme == "http" && port == 80) ||
                      (scheme == "https" && port == 443))) {
        netloc = host + QLatin1Char(':') + QString::number(port);
    }

    const QString path = u.path().isEmpty() ? "/" : u.path();
    const QString query = u.query(QUrl::FullyEncoded);

    QString out = scheme + "://" + netloc + path;
    if (!query.isEmpty()) out += QLatin1Char('?') + query;  // fragment intentionally dropped
    return out;
}

// ---------------------------------------------------------------------------
// ref:// tokens
// ---------------------------------------------------------------------------

bool is_ref_token(const QString& value) {
    return starts_with_ci(value.trimmed(), kRefScheme);
}

QString unknown_ref_error(const QString& token) {
    return QString("Error: Unknown link reference '%1'. Only ref:// tokens returned by "
                   "this server's search results can be expanded, and they are forgotten "
                   "when the server restarts. Run the search again to get a fresh token.")
        .arg(token.trimmed());
}

// ---------------------------------------------------------------------------
// Retry-After
// ---------------------------------------------------------------------------

std::optional<double> retry_after_seconds(const QMap<QString, QString>& headers) {
    const auto it = headers.constFind("retry-after");
    if (it == headers.cend()) return std::nullopt;
    bool         ok = false;
    const double v  = it.value().trimmed().toDouble(&ok);
    if (!ok) return std::nullopt;  // includes date-formatted values
    return std::max(0.0, v);
}

double sleep_retry_after(const QMap<QString, QString>& headers, double default_seconds) {
    double wait = retry_after_seconds(headers).value_or(default_seconds);
    wait        = std::min(wait, 30.0);
    if (wait > 0) std::this_thread::sleep_for(std::chrono::duration<double>(wait));
    return wait;
}

// ---------------------------------------------------------------------------
// SSRF guard
// ---------------------------------------------------------------------------

namespace {

bool is_disallowed_v4(quint32 ip) {
    const quint32 b0 = (ip >> 24) & 0xff;
    const quint32 b1 = (ip >> 16) & 0xff;
    const quint32 b2 = (ip >> 8) & 0xff;
    if (b0 == 0)                            return true;  // 0.0.0.0/8
    if (b0 == 10)                           return true;  // RFC1918
    if (b0 == 127)                          return true;  // loopback
    if (b0 == 169 && b1 == 254)             return true;  // link-local / metadata
    if (b0 == 172 && b1 >= 16 && b1 <= 31)  return true;  // RFC1918
    if (b0 == 192 && b1 == 168)             return true;  // RFC1918
    if (b0 == 100 && b1 >= 64 && b1 <= 127) return true;  // CGNAT 100.64/10
    if (b0 == 192 && b1 == 0 && b2 == 0)    return true;  // 192.0.0.0/24
    if (b0 == 192 && b1 == 0 && b2 == 2)    return true;  // TEST-NET-1
    if (b0 == 198 && (b1 == 18 || b1 == 19)) return true; // 198.18.0.0/15
    if (b0 == 198 && b1 == 51 && b2 == 100) return true;  // TEST-NET-2
    if (b0 == 203 && b1 == 0 && b2 == 113)  return true;  // TEST-NET-3
    if (b0 >= 224)                          return true;  // multicast + reserved + broadcast
    return false;
}

bool is_disallowed(const QHostAddress& addr) {
    if (addr.protocol() == QAbstractSocket::IPv4Protocol) {
        return is_disallowed_v4(addr.toIPv4Address());
    }
    bool          ok     = false;
    const quint32 mapped = addr.toIPv4Address(&ok);  // IPv4-mapped IPv6
    if (ok) return is_disallowed_v4(mapped);

    if (addr.isLoopback() || addr.isLinkLocal() || addr.isSiteLocal() ||
        addr.isUniqueLocalUnicast() || addr.isMulticast() || addr.isBroadcast()) {
        return true;
    }
    if (addr == QHostAddress(QHostAddress::AnyIPv6) ||
        addr == QHostAddress(QHostAddress::Any)) {
        return true;
    }
    return !addr.isGlobal();
}

}  // namespace

void validate_public_url(const QString& url) {
    const QUrl    u(url);
    const QString scheme = u.scheme().toLower();
    if (scheme != "http" && scheme != "https") {
        throw BlockedUrlError(
            QString("unsupported URL scheme '%1://' (only http and https are allowed)")
                .arg(u.scheme()));
    }

    const QString host = u.host();
    if (host.isEmpty()) throw BlockedUrlError("URL has no host");

    const QString lowered = host.toLower();
    if (lowered == "localhostQString(" ||
        lowered.endsWith(").localhost")) {
        throw BlockedUrlError(
            QString("refusing to fetch loopback host '%1'").arg(host));
    }

    const int port = u.port(scheme == "https" ? 443 : 80);
    if (port <= 0 || port > 65535) {
        throw BlockedUrlError(QString("invalid port in URL '%1'").arg(url));
    }

    const QHostInfo info = QHostInfo::fromName(host);
    if (info.error() != QHostInfo::NoError || info.addresses().isEmpty()) {
        throw BlockedUrlError(QString("could not resolve host '%1': %2")
                                  .arg(host, info.errorString()));
    }
    for (const QHostAddress& addr : info.addresses()) {
        if (is_disallowed(addr)) {
            throw BlockedUrlError(
                QString("refusing to fetch '%1' — it resolves to non-public address %2")
                    .arg(host, addr.toString()));
        }
    }
}

}  // namespace ddg
