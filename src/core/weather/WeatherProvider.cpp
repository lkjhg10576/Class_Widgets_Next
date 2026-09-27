#include "WeatherProvider.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

WeatherProvider::WeatherProvider(QNetworkAccessManager *nam, QObject *parent)
    : QObject(parent)
    , m_nam(nam)
{
}

void WeatherProvider::getJson(const QUrl &url,
                              const std::list<std::pair<QString, QString>> &headers, int timeoutMs,
                              const JsonCallback &callback)
{
    QNetworkRequest request(url);
    request.setTransferTimeout(timeoutMs);
    for (const auto &header : headers)
        request.setRawHeader(header.first.toUtf8(), header.second.toUtf8());

    QNetworkReply *reply = m_nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback] {
        reply->deleteLater();
        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            callback({}, errorKindForHttp(status), status);
            return;
        }
        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError
            || (!doc.isObject() && !doc.isArray())) {
            callback({}, QStringLiteral("parse"), status);
            return;
        }
        callback(doc, QString(), status);
    });
}

QString WeatherProvider::errorKindForHttp(int httpStatus)
{
    if (httpStatus == 401 || httpStatus == 403)
        return QStringLiteral("auth");
    if (httpStatus == 402 || httpStatus == 429)
        return QStringLiteral("quota");
    return QStringLiteral("network");
}
