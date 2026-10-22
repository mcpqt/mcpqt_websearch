// SPDX-License-Identifier: Apache-2.0
//
// HttpClient transport backed by the system `curl` binary. Compiled when
// DDG_HTTP_BACKEND == "curl". Note: this uses the plain curl binary, not the
// Chrome-TLS-impersonating curl_cffi the Python server can use.

#include "http_client.hpp"

#include "connect_error.hpp"
#include "http_error.hpp"
#include "timeout_error.hpp"

#include <QProcess>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cctype>

namespace ddg {
namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

HttpResponse parse_curl_output(const std::string& raw) {
    HttpResponse res;
    const std::size_t header_end = raw.find("\r\n\r\n");
    const std::string head  = header_end == std::string::npos ? raw : raw.substr(0, header_end);
    res.body = header_end == std::string::npos ? std::string{} : raw.substr(header_end + 4);

    std::size_t pos   = 0;
    bool        first = true;
    while (pos <= head.size()) {
        const std::size_t nl   = head.find("\r\n", pos);
        std::string       line = head.substr(pos, nl == std::string::npos ? std::string::npos
                                                                           : nl - pos);
        if (first) {
            const std::size_t sp = line.find(' ');
            if (sp != std::string::npos) {
                try {
                    res.status = std::stoi(line.substr(sp + 1));
                } catch (...) {
                    res.status = 0;
                }
            }
            first = false;
        } else if (!line.empty()) {
            const std::size_t colon = line.find(':');
            if (colon != std::string::npos) {
                std::string key = lower(line.substr(0, colon));
                std::string val = line.substr(colon + 1);
                const auto  not_space = [](unsigned char c) { return !std::isspace(c); };
                val.erase(val.begin(), std::find_if(val.begin(), val.end(), not_space));
                val.erase(std::find_if(val.rbegin(), val.rend(), not_space).base(), val.end());
                res.headers[key] = val;
            }
        }
        if (nl == std::string::npos) break;
        pos = nl + 2;
    }
    auto loc = res.headers.find("location");
    if (loc != res.headers.end()) res.location = loc->second;
    return res;
}

}  // namespace

const char* HttpClient::backend_name() { return "curl"; }

HttpResponse HttpClient::perform(const std::string&                        method,
                                 const std::string&                        url,
                                 const std::map<std::string, std::string>& headers,
                                 const std::string&                        body,
                                 int                                       timeout_ms) {
    const QString curl = QStandardPaths::findExecutable("curl");
    if (curl.isEmpty()) {
        throw HttpError("The 'curl' backend requires the `curl` executable, which was "
                        "not found on PATH.");
    }

    QStringList args;
    args << "-s" << "-i";  // silent, include response headers
    args << "--max-time" << QString::number(std::max(1, timeout_ms / 1000));
    if (!verify_.enabled) {
        args << "-k";
    } else if (!verify_.ca_bundle.empty()) {
        args << "--cacert" << QString::fromStdString(verify_.ca_bundle);
    }
    for (const auto& [k, v] : headers) {
        args << "-H" << QString::fromStdString(k + ": " + v);
    }
    if (method == "POST") {
        args << "--data" << QString::fromStdString(body);
    }
    args << QString::fromStdString(url);

    QProcess proc;
    proc.start(curl, args);
    if (!proc.waitForStarted(5000)) {
        throw ConnectError("failed to launch curl");
    }
    if (!proc.waitForFinished(timeout_ms + 5000)) {
        proc.kill();
        throw TimeoutError("curl request timed out");
    }
    if (proc.exitStatus() != QProcess::NormalExit) {
        throw ConnectError("curl terminated abnormally");
    }
    if (proc.exitCode() == 28) {
        throw TimeoutError("curl: operation timed out");
    }
    if (proc.exitCode() != 0) {
        const std::string err = proc.readAllStandardError().toStdString();
        throw ConnectError(err.empty() ? "curl failed" : err);
    }

    HttpResponse res = parse_curl_output(proc.readAllStandardOutput().toStdString());
    if (res.status == 0) throw ConnectError("could not parse curl response");
    return res;
}

}  // namespace ddg
