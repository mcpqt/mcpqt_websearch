// SPDX-License-Identifier: AGPL-3.0-only
// SPDX-License-Identifier: Apache-2.0

#include "utils.hpp"

#include "blocked_url_error.hpp"
#include "constants.hpp"

#include <QHostAddress>
#include <QHostInfo>
#include <QString>
#include <QUrl>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <regex>
#include <sstream>
#include <thread>

namespace ddg {

// ---------------------------------------------------------------------------
// Strings
// ---------------------------------------------------------------------------

std::string trim(std::string s) {
    const auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool starts_with_ci(const std::string& haystack, const std::string& needle) {
    if (haystack.size() < needle.size()) return false;
    for (std::size_t i = 0; i < needle.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(haystack[i])) !=
            std::tolower(static_cast<unsigned char>(needle[i]))) {
            return false;
        }
    }
    return true;
}

std::string strip_slashes(std::string s) {
    std::size_t b = 0, e = s.size();
    while (b < e && s[b] == '/') ++b;
    while (e > b && s[e - 1] == '/') --e;
    return s.substr(b, e - b);
}

bool contains(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

std::string collapse_whitespace(const std::string& text) {
    // lines = (line.strip() for line in text.splitlines())
    // chunks = (phrase.strip() for line in lines for phrase in line.split("  "))
    // " ".join(chunk for chunk in chunks if chunk)  then  re.sub(r"\s+", " ", ...)
    std::istringstream       lines(text);
    std::string              line;
    std::vector<std::string> chunks;
    while (std::getline(lines, line)) {
        line             = trim(line);
        std::size_t pos  = 0;
        while (pos <= line.size()) {
            const std::size_t next = line.find("  ", pos);
            std::string       phrase =
                line.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
            phrase = trim(phrase);
            if (!phrase.empty()) chunks.push_back(phrase);
            if (next == std::string::npos) break;
            pos = next + 2;
        }
    }
    std::string joined;
    for (std::size_t i = 0; i < chunks.size(); ++i) {
        if (i) joined += ' ';
        joined += chunks[i];
    }
    joined = std::regex_replace(joined, std::regex("\\s+"), " ");
    return trim(joined);
}

// ---------------------------------------------------------------------------
// URLs
// ---------------------------------------------------------------------------

std::string url_join(const std::string& base, const std::string& location) {
    const QUrl b(QString::fromStdString(base));
    const QUrl r(QString::fromStdString(location));
    return b.resolved(r).toString().toStdString();
}

std::string url_form_encode(const std::map<std::string, std::string>& form) {
    std::string out;
    for (const auto& [k, v] : form) {
        if (!out.empty()) out += '&';
        out += QUrl::toPercentEncoding(QString::fromStdString(k)).toStdString();
        out += '=';
        out += QUrl::toPercentEncoding(QString::fromStdString(v)).toStdString();
    }
    return out;
}

std::string url_hostname(const std::string& url) {
    return to_lower(QUrl(QString::fromStdString(url)).host().toStdString());
}

std::string normalize_cache_url(const std::string& url) {
    const QUrl u(QString::fromStdString(url));
    const std::string scheme = (u.scheme().isEmpty() ? "http" : u.scheme().toLower()).toStdString();
    const std::string host   = u.host().toLower().toStdString();
    const int         port   = u.port(-1);
    std::string       netloc = host;
    if (port > 0 && !((scheme == "http" && port == 80) || (scheme == "https" && port == 443))) {
        netloc = host + ":" + std::to_string(port);
    }
    const std::string path  = (u.path().isEmpty() ? "/" : u.path()).toStdString();
    const QString     query = u.query(QUrl::FullyEncoded);

    std::string out = scheme + "://" + netloc + path;
    if (!query.isEmpty()) out += "?" + query.toStdString();  // fragment intentionally dropped
    return out;
}

// ---------------------------------------------------------------------------
// ref:// tokens
// ---------------------------------------------------------------------------

bool is_ref_token(const std::string& value) {
    return starts_with_ci(trim(value), kRefScheme);
}

std::string unknown_ref_error(const std::string& token) {
    return "Error: Unknown link reference '" + trim(token) + "'. Only ref:// tokens "
           "returned by this server's search results can be expanded, and they are "
           "forgotten when the server restarts. Run the search again to get a fresh token.";
}

// ---------------------------------------------------------------------------
// Retry-After
// ---------------------------------------------------------------------------

std::optional<double> retry_after_seconds(
    const std::unordered_map<std::string, std::string>& headers) {
    auto it = headers.find("retry-after");
    if (it == headers.end()) return std::nullopt;
    try {
        double v = std::stod(trim(it->second));
        return std::max(0.0, v);
    } catch (...) {
        return std::nullopt;  // includes date-formatted values
    }
}

double sleep_retry_after(const std::unordered_map<std::string, std::string>& headers,
                         double                                            default_seconds) {
    double wait = retry_after_seconds(headers).value_or(default_seconds);
    wait = std::min(wait, 30.0);
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

void validate_public_url(const std::string& url) {
    const QUrl        u(QString::fromStdString(url));
    const std::string scheme = u.scheme().toLower().toStdString();
    if (scheme != "http" && scheme != "https") {
        throw BlockedUrlError("unsupported URL scheme '" + u.scheme().toStdString() +
                              "://' (only http and https are allowed)");
    }
    const QString host_q = u.host();
    if (host_q.isEmpty()) throw BlockedUrlError("URL has no host");
    const std::string host = host_q.toStdString();

    const std::string lowered = to_lower(host);
    if (lowered == "localhost" || lowered.ends_with(".localhost")) {
        throw BlockedUrlError("refusing to fetch loopback host '" + host + "'");
    }

    const int port = u.port(scheme == "https" ? 443 : 80);
    if (port <= 0 || port > 65535) {
        throw BlockedUrlError("invalid port in URL '" + url + "'");
    }

    const QHostInfo info = QHostInfo::fromName(host_q);
    if (info.error() != QHostInfo::NoError || info.addresses().isEmpty()) {
        throw BlockedUrlError("could not resolve host '" + host + "': " +
                              info.errorString().toStdString());
    }
    for (const QHostAddress& addr : info.addresses()) {
        if (is_disallowed(addr)) {
            throw BlockedUrlError("refusing to fetch '" + host +
                                  "' — it resolves to non-public address " +
                                  addr.toString().toStdString());
        }
    }
}

}  // namespace ddg
