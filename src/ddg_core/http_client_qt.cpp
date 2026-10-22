// SPDX-License-Identifier: Apache-2.0
//
// HttpClient transport backed by QNetworkAccessManager. Compiled when
// DDG_HTTP_BACKEND == "qt".

#include "http_client.hpp"

#include "connect_error.hpp"
#include "http_error.hpp"
#include "timeout_error.hpp"

#include <QByteArray>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QString>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QUrl>

#include <algorithm>
#include <cctype>

namespace ddg {
namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

void configure_ssl(QNetworkRequest& req, const TlsVerify& verify) {
    QSslConfiguration cfg = req.sslConfiguration();
    if (!verify.enabled) {
        cfg.setPeerVerifyMode(QSslSocket::VerifyNone);
    } else if (!verify.ca_bundle.empty()) {
        const auto certs = QSslCertificate::fromPath(QString::fromStdString(verify.ca_bundle));
        cfg.setCaCertificates(certs);
    }
    req.setSslConfiguration(cfg);
}

}  // namespace

const char* HttpClient::backend_name() { return "qt"; }

HttpResponse HttpClient::perform(const std::string&                        method,
                                 const std::string&                        url,
                                 const std::map<std::string, std::string>& headers,
                                 const std::string&                        body,
                                 int                                       timeout_ms) {
    QNetworkAccessManager nam;
    QNetworkRequest req{QUrl(QString::fromStdString(url))};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::ManualRedirectPolicy);
    req.setTransferTimeout(timeout_ms);
    for (const auto& [k, v] : headers) {
        req.setRawHeader(QByteArray::fromStdString(k), QByteArray::fromStdString(v));
    }
    configure_ssl(req, verify_);

    QNetworkReply* reply = (method == "POST") ? nam.post(req, QByteArray::fromStdString(body))
                                              : nam.get(req);

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const QVariant status_attr = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    const int      status      = status_attr.isValid() ? status_attr.toInt() : 0;

    HttpResponse res;
    res.status = status;
    for (const auto& p : reply->rawHeaderPairs()) {
        res.headers[lower(p.first.toStdString())] = p.second.toStdString();
    }
    auto loc = res.headers.find("location");
    if (loc != res.headers.end()) res.location = loc->second;
    res.body = reply->readAll().toStdString();

    if (status == 0) {
        const QNetworkReply::NetworkError err    = reply->error();
        const std::string                 detail = reply->errorString().toStdString();
        if (err == QNetworkReply::TimeoutError || err == QNetworkReply::OperationCanceledError) {
            throw TimeoutError("request timed out: " + detail);
        }
        if (err == QNetworkReply::ConnectionRefusedError ||
            err == QNetworkReply::RemoteHostClosedError ||
            err == QNetworkReply::HostNotFoundError ||
            err == QNetworkReply::NetworkSessionFailedError ||
            err == QNetworkReply::TemporaryNetworkFailureError ||
            err == QNetworkReply::UnknownNetworkError) {
            throw ConnectError(detail.empty() ? "connection failed" : detail);
        }
        throw HttpError(detail.empty() ? "network error" : detail);
    }
    return res;
}

}  // namespace ddg
