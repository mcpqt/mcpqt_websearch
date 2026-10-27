// SPDX-License-Identifier: AGPL-3.0-only

#include "webio_transport.hpp"

#include "error.hpp"

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

// The host converts each argument to the slot's declared type. A QVariantMap
// argument cannot be converted to a QVariant parameter by QVariant::convert,
// so wrap the map in a QVariant-typed QVariant before crossing the host ABI.
QVariant wrap_variant(const QVariant& value) {
    QVariant inner = value;
    return QVariant(QMetaType::fromType<QVariant>(), &inner);
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
    if (lower.contains("timed out")) {
        throw TimeoutError(message);
    }
    if (lower.contains("connection") ||
        lower.contains("refused") ||
        lower.contains("host not found") ||
        lower.contains("network")) {
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
                    "QFuture<QVariant> WebioPlugin::httpPost(QString,QVariant,QVariant,uint32_t)",
                    { QVariant(url), wrap_variant(QVariant(to_variant_map(request.form))),
                      wrap_variant(QVariant(headers)),
                      QVariant(static_cast<uint>(request.timeout_ms)) });
            } else {
                handle = call_func(
                    "QFuture<QVariant> WebioPlugin::httpGet(QString,QVariant,uint32_t)",
                    { QVariant(url), wrap_variant(QVariant(headers)),
                      QVariant(static_cast<uint>(request.timeout_ms)) });
            }
        } catch (const std::exception& e) {
            const QVariant w_headers = wrap_variant(QVariant(headers));
            throw HttpError(QString("webio call failed: %1 [url=%2 headers=%3 inner=%4 timeout=%5]")
                                .arg(QString::fromUtf8(e.what()))
                                .arg(QVariant(url).metaType().name())
                                .arg(w_headers.metaType().name())
                                .arg(w_headers.value<QVariant>().metaType().name())
                                .arg(QVariant(static_cast<uint>(request.timeout_ms)).metaType().name()));
        }

        QFuture<QVariant> future;
        try {
            future = handle.value<QFuture<QVariant>>();
        } catch (const std::exception& e) {
            throw HttpError(QString("webio returned an invalid future: %1")
                                .arg(QString::fromUtf8(e.what())));
        }
        if (!future.isValid()) {
            throw HttpError(QString("webio returned an invalid future (handle type=%1 valid=%2 convertible=%3)")
                .arg(QString::fromLatin1(handle.metaType().name()))
                .arg(handle.isValid() ? "yes" : "no")
                .arg(handle.canConvert<QFuture<QVariant>>() ? "yes"
                                                             : "no"));
        }

        QVariant resolved;
        try {
            resolved = wait_for_future(future);
        } catch (const std::exception& e) {
            throw HttpError(QString("webio request failed: %1")
                                .arg(QString::fromUtf8(e.what())));
        }

        const QVariantMap map = resolved.toMap();

        // webio's plugin wrapper may set ok=true even for network failures, so
        // treat the presence of an error as authoritative.
        if (map.contains("error")) {
            throw_transport_error(map.value("error").toString());
        }
        if (!map.contains("status")) {
            throw HttpError("webio returned an invalid response");
        }

        HttpResponse response;
        response.status   = map.value("status").toInt();
        response.body     = map.value("body").toString();
        response.location = map.value("location").toString();

        const QVariantMap response_headers = map.value("headers").toMap();
        for (auto it = response_headers.cbegin(); it != response_headers.cend(); ++it) {
            response.headers.insert(it.key().toLower(), it.value().toString());
        }
        return response;
    };
}

}  // namespace ddg
