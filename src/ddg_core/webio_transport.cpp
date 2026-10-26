// SPDX-License-Identifier: AGPL-3.0-only

#include "webio_transport.hpp"

#include "connect_error.hpp"
#include "http_error.hpp"
#include "timeout_error.hpp"

#include <QEventLoop>
#include <QFuture>
#include <QFutureWatcher>
#include <QMap>
#include <QObject>
#include <QString>
#include <QVariantMap>

#include <exception>
#include <utility>

namespace ddg {
namespace {

QVariant wait_for_future(QFuture<QVariant> future) {
    if (future.isFinished()) {
        return future.result();
    }

    QEventLoop               loop;
    QFutureWatcher<QVariant> watcher;
    QObject::connect(&watcher, &QFutureWatcher<QVariant>::finished,
                     &loop, &QEventLoop::quit);
    watcher.setFuture(future);
    if (!future.isFinished()) {
        loop.exec();
    }
    return future.result();
}

QVariantMap to_variant_map(const QMap<QString, QString>& values) {
    QVariantMap map;
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        map.insert(it.key(), it.value());
    }
    return map;
}

[[noreturn]] void throw_transport_error(const QString& message) {
    const QString lower = message.toLower();
    if (lower.contains(QStringLiteral("timed out"))) {
        throw TimeoutError(message);
    }
    if (lower.contains(QStringLiteral("connection")) ||
        lower.contains(QStringLiteral("refused")) ||
        lower.contains(QStringLiteral("host not found")) ||
        lower.contains(QStringLiteral("network"))) {
        throw ConnectError(message);
    }
    throw HttpError(message);
}

}  // namespace

Transport make_webio_transport(HostCallFunc call_func) {
    return [call_func = std::move(call_func)](const HttpRequest& request) -> HttpResponse {
        const QString     url     = request.url;
        const QVariantMap headers = to_variant_map(request.headers);

        QVariant handle;
        try {
            if (request.is_post) {
                handle = call_func(
                    QStringLiteral(
                        "QFuture<QVariant> WebioPlugin::httpPost(QString,QVariant,QVariant)"),
                    { QVariant(url), QVariant(to_variant_map(request.form)),
                      QVariant(headers) });
            } else {
                handle = call_func(
                    QStringLiteral(
                        "QFuture<QVariant> WebioPlugin::httpGet(QString,QVariant)"),
                    { QVariant(url), QVariant(headers) });
            }
        } catch (const std::exception& e) {
            throw HttpError(QStringLiteral("webio call failed: %1")
                                .arg(QString::fromUtf8(e.what())));
        }

        QFuture<QVariant> future;
        try {
            future = handle.value<QFuture<QVariant>>();
        } catch (const std::exception& e) {
            throw HttpError(QStringLiteral("webio returned an invalid future: %1")
                                .arg(QString::fromUtf8(e.what())));
        }
        if (!future.isValid()) {
            throw HttpError(QStringLiteral("webio returned an invalid future"));
        }

        QVariant resolved;
        try {
            resolved = wait_for_future(future);
        } catch (const std::exception& e) {
            throw HttpError(QStringLiteral("webio request failed: %1")
                                .arg(QString::fromUtf8(e.what())));
        }

        const QVariantMap map = resolved.toMap();

        // webio's plugin wrapper may set ok=true even for network failures, so
        // treat the presence of an error as authoritative.
        if (map.contains(QStringLiteral("error"))) {
            throw_transport_error(map.value(QStringLiteral("error")).toString());
        }
        if (!map.contains(QStringLiteral("status"))) {
            throw HttpError(QStringLiteral("webio returned an invalid response"));
        }

        HttpResponse response;
        response.status   = map.value(QStringLiteral("status")).toInt();
        response.body     = map.value(QStringLiteral("body")).toString();
        response.location = map.value(QStringLiteral("location")).toString();

        const QVariantMap response_headers = map.value(QStringLiteral("headers")).toMap();
        for (auto it = response_headers.cbegin(); it != response_headers.cend(); ++it) {
            response.headers.insert(it.key().toLower(), it.value().toString());
        }
        return response;
    };
}

}  // namespace ddg
