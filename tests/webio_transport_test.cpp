// SPDX-License-Identifier: AGPL-3.0-only
//
// Unit tests for the webio transport adapter. Uses a fake host callFunc that
// returns already-finished futures, so no network or event loop is needed.

#include "webio_transport.hpp"

#include "connect_error.hpp"
#include "http_error.hpp"
#include "timeout_error.hpp"

#include <QFuture>
#include <QPromise>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <cstdio>
#include <stdexcept>
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

QVariant finished_future(const QVariantMap& result) {
    QPromise<QVariant> promise;
    promise.start();
    promise.addResult(result);
    promise.finish();
    return QVariant::fromValue(promise.future());
}

struct Call {
    QString      signature;
    QVariantList args;
};

}  // namespace

int main() {
    // GET conversion and argument marshalling.
    {
        Call call;
        const ddg::Transport transport = ddg::make_webio_transport(
            [&call](const QString& signature, const QVariantList& args) -> QVariant {
                call.signature = signature;
                call.args      = args;
                QVariantMap headers;
                headers.insert(QStringLiteral("content-type"),
                               QStringLiteral("text/plain"));
                QVariantMap result;
                result.insert(QStringLiteral("status"), 200);
                result.insert(QStringLiteral("body"), QStringLiteral("hello"));
                result.insert(QStringLiteral("headers"), headers);
                result.insert(QStringLiteral("location"), QString());
                result.insert(QStringLiteral("ok"), true);
                return finished_future(result);
            });

        ddg::HttpRequest request;
        request.url     = "https://example.com/";
        request.headers = {{"X-Test", "value"}};
        const ddg::HttpResponse response = transport(request);

        check(call.signature ==
                  QStringLiteral(
                      "QFuture<QVariant> WebioPlugin::httpGet(QString,QVariant)"),
              "GET signature");
        check(call.args.size() == 2, "GET argument count");
        check(call.args.at(0).toString() == QStringLiteral("https://example.com/"),
              "GET url");
        const QVariantMap sent_headers = call.args.at(1).toMap();
        check(sent_headers.value(QStringLiteral("X-Test")).toString() ==
                  QStringLiteral("value"),
              "GET request header");
        check(response.status == 200, "GET status");
        check(response.body == "hello", "GET body");
        check(response.header("content-type") == "text/plain",
              "GET response header");
    }

    // POST conversion and argument marshalling.
    {
        Call call;
        const ddg::Transport transport = ddg::make_webio_transport(
            [&call](const QString& signature, const QVariantList& args) -> QVariant {
                call.signature = signature;
                call.args      = args;
                QVariantMap result;
                result.insert(QStringLiteral("status"), 201);
                result.insert(QStringLiteral("body"), QStringLiteral("posted"));
                result.insert(QStringLiteral("headers"), QVariantMap{});
                result.insert(QStringLiteral("ok"), true);
                return finished_future(result);
            });

        ddg::HttpRequest request;
        request.is_post = true;
        request.url     = "https://example.com/api";
        request.form    = {{"q", "term"}};
        const ddg::HttpResponse response = transport(request);

        check(call.signature ==
                  QStringLiteral(
                      "QFuture<QVariant> WebioPlugin::httpPost(QString,QVariant,QVariant)"),
              "POST signature");
        check(call.args.size() == 3, "POST argument count");
        const QVariantMap form = call.args.at(1).toMap();
        check(form.value(QStringLiteral("q")).toString() == QStringLiteral("term"),
              "POST form");
        check(response.status == 201, "POST status");
        check(response.body == "posted", "POST body");
    }

    // webio may leave ok=true on network failure; error must win.
    {
        const ddg::Transport transport = ddg::make_webio_transport(
            [](const QString&, const QVariantList&) -> QVariant {
                QVariantMap result;
                result.insert(QStringLiteral("ok"), true);
                result.insert(QStringLiteral("error"),
                              QStringLiteral("request timed out: slow"));
                return finished_future(result);
            });
        ddg::HttpRequest request;
        request.url = "https://example.com/";
        bool caught = false;
        try {
            transport(request);
        } catch (const ddg::TimeoutError&) {
            caught = true;
        } catch (...) {
        }
        check(caught, "timeout error mapping");
    }

    {
        const ddg::Transport transport = ddg::make_webio_transport(
            [](const QString&, const QVariantList&) -> QVariant {
                QVariantMap result;
                result.insert(QStringLiteral("error"),
                              QStringLiteral("Connection refused"));
                return finished_future(result);
            });
        ddg::HttpRequest request;
        request.url = "https://example.com/";
        bool caught = false;
        try {
            transport(request);
        } catch (const ddg::ConnectError&) {
            caught = true;
        } catch (...) {
        }
        check(caught, "connect error mapping");
    }

    // A throwing host call becomes an HttpError.
    {
        const ddg::Transport transport = ddg::make_webio_transport(
            [](const QString&, const QVariantList&) -> QVariant {
                throw std::runtime_error("webio not loaded");
            });
        ddg::HttpRequest request;
        request.url = "https://example.com/";
        bool caught = false;
        try {
            transport(request);
        } catch (const ddg::HttpError&) {
            caught = true;
        } catch (...) {
        }
        check(caught, "missing webio mapping");
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d transport test(s) failed\n", g_failures);
        return 1;
    }
    std::printf("webio transport tests passed\n");
    return 0;
}
