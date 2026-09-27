#include "CaiyunProvider.h"

#include "../../ConfigStore.h"
#include "../WeatherCodes.h"

#include <QDateTime>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QUrlQuery>

CaiyunProvider::CaiyunProvider(ConfigStore *configs, QNetworkAccessManager *nam, QObject *parent)
    : WeatherProvider(nam, parent)
    , m_configs(configs)
{
}

QString CaiyunProvider::token() const
{
    if (m_configs) {
        if (const auto v = m_configs->value(QStringLiteral("weather.keys.caiyun.token")))
            return v->toString().trimmed();
    }
    return {};
}

bool CaiyunProvider::isConfigured() const
{
    return !token().isEmpty();
}

QString CaiyunProvider::errorKindFromBody(const QJsonObject &body) const
{
    if (body.value(QLatin1String("status")).toString() == QLatin1String("ok"))
        return {};
    const QString error = body.value(QLatin1String("error")).toString();
    if (error.contains(QLatin1String("invalid"), Qt::CaseInsensitive)
        || error.contains(QLatin1String("token"), Qt::CaseInsensitive))
        return QStringLiteral("auth");
    return QStringLiteral("quota");
}

void CaiyunProvider::searchCity(const QString &keyword)
{
    Q_UNUSED(keyword);
    // 彩云无城市搜索接口；门面已把搜索路由到小米，此实现仅为兜底
    emit citySearchFinished({});
}

void CaiyunProvider::fetch(const CityInfo &city)
{
    // 综合接口：URL 路径 {经度},{纬度}（经度在前！）
    QUrl url(QStringLiteral("https://api.caiyunapp.com/v2.6/") + token() + QLatin1Char('/')
             + QString::number(city.longitude, 'g', 12) + QLatin1Char(',')
             + QString::number(city.latitude, 'g', 12) + QStringLiteral("/weather"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("alert"), QStringLiteral("true"));
    query.addQueryItem(QStringLiteral("dailysteps"), QStringLiteral("3"));
    query.addQueryItem(QStringLiteral("hourlysteps"), QStringLiteral("24"));
    query.addQueryItem(QStringLiteral("unit"), QStringLiteral("metric"));
    query.addQueryItem(QStringLiteral("lang"), QStringLiteral("zh_CN"));
    url.setQuery(query);
    fetchUrl(url, city.cityId, /*test=*/false);
}

void CaiyunProvider::fetchUrl(const QUrl &url, const QString &cityId, bool test)
{
    getJson(url, {}, cwn::weather::kFetchTimeoutMs,
            [this, cityId, test](const QJsonDocument &doc, const QString &errorKind, int) {
                if (!errorKind.isEmpty()) {
                    if (test)
                        emit connectionTestFinished(false, errorKind);
                    else
                        emit fetchFinished(cityId, {}, errorKind);
                    return;
                }
                const QJsonObject body = doc.object();
                const QString bodyKind = errorKindFromBody(body);
                if (!bodyKind.isEmpty()) {
                    if (test)
                        emit connectionTestFinished(false, bodyKind);
                    else
                        emit fetchFinished(cityId, {}, bodyKind);
                    return;
                }
                if (test) {
                    emit connectionTestFinished(true, QString());
                    return;
                }

                const QJsonObject result = body.value(QLatin1String("result")).toObject();
                const QJsonObject realtime =
                    result.value(QLatin1String("realtime")).toObject();

                // 现象码按时段选 skycon：白天用 08h-20h、夜间用 20h-32h 档，回退 skycon
                const int hour = QDateTime::currentDateTime().time().hour();
                const QString skyconKey =
                    (hour >= 8 && hour < 20) ? QStringLiteral("skycon_08h_20h")
                                             : QStringLiteral("skycon_20h_32h");
                QString skycon = realtime.value(skyconKey).toString();
                if (skycon.isEmpty())
                    skycon = realtime.value(QLatin1String("skycon")).toString();

                QVariantMap current;
                if (realtime.contains(QLatin1String("temperature"))) {
                    current.insert(QStringLiteral("temperature"),
                                   realtime.value(QLatin1String("temperature")).toDouble());
                    current.insert(QStringLiteral("weatherCode"),
                                   cwn::weather::codeFromCaiyunSkycon(skycon));
                    if (realtime.contains(QLatin1String("apparent_temperature")))
                        current.insert(QStringLiteral("feelsLike"),
                                       realtime.value(QLatin1String("apparent_temperature"))
                                           .toDouble());
                    // 湿度 0-1 比率 → 0-100
                    current.insert(QStringLiteral("humidity"),
                                   qBound(0.0, realtime.value(QLatin1String("humidity"))
                                                       .toDouble()
                                                   * 100.0,
                                          100.0));
                    current.insert(QStringLiteral("windSpeed"),
                                   realtime.value(QLatin1String("wind"))
                                       .toObject()
                                       .value(QLatin1String("speed"))
                                       .toDouble());
                }

                QVariantMap today;
                const QJsonObject daily = result.value(QLatin1String("daily")).toObject();
                const auto firstOf = [&daily](const QString &key) {
                    const QJsonArray arr = daily.value(key).toArray();
                    return arr.isEmpty() ? QJsonObject() : arr.at(0).toObject();
                };
                const QJsonObject temp0 = firstOf(QStringLiteral("temperature"));
                if (temp0.contains(QLatin1String("max"))) {
                    today.insert(QStringLiteral("tempMax"), temp0.value(QLatin1String("max")).toDouble());
                    today.insert(QStringLiteral("tempMin"), temp0.value(QLatin1String("min")).toDouble());
                    const int dayCode = cwn::weather::codeFromCaiyunSkycon(
                        firstOf(QStringLiteral("skycon_08h_20h")).value(QLatin1String("value")).toString());
                    const int nightCode = cwn::weather::codeFromCaiyunSkycon(
                        firstOf(QStringLiteral("skycon_20h_32h")).value(QLatin1String("value")).toString());
                    today.insert(QStringLiteral("dayCode"), dayCode);
                    today.insert(QStringLiteral("nightCode"), nightCode);
                    const QJsonObject precip0 = firstOf(QStringLiteral("precipitation"));
                    if (precip0.contains(QLatin1String("probability")))
                        today.insert(QStringLiteral("precipProb"),
                                     qBound(0.0, precip0.value(QLatin1String("probability"))
                                                         .toDouble()
                                                     * 100.0,
                                            100.0));
                    const QJsonObject astro0 = firstOf(QStringLiteral("astro"));
                    const QString sunrise =
                        astro0.value(QLatin1String("sunrise"))
                            .toObject()
                            .value(QLatin1String("time"))
                            .toString(); // 已是 "HH:MM"
                    const QString sunset =
                        astro0.value(QLatin1String("sunset"))
                            .toObject()
                            .value(QLatin1String("time"))
                            .toString();
                    if (!sunrise.isEmpty())
                        today.insert(QStringLiteral("sunrise"), sunrise);
                    if (!sunset.isEmpty())
                        today.insert(QStringLiteral("sunset"), sunset);
                }

                // 预警：result.alert.content[]（无权限时缺省）；等级从标题颜色词归一；
                // alertId 缺失时用 title+time 哈希兜底（会话内去重键）
                QVariantList alerts;
                const QJsonArray alertContent =
                    result.value(QLatin1String("alert"))
                        .toObject()
                        .value(QLatin1String("content"))
                        .toArray();
                for (const QJsonValue &item : alertContent) {
                    const QJsonObject obj = item.toObject();
                    const QString title = obj.value(QLatin1String("title")).toString();
                    if (title.isEmpty())
                        continue;
                    QString alertId = obj.value(QLatin1String("alertId")).toString();
                    const qint64 time = static_cast<qint64>(
                        obj.value(QLatin1String("time")).toDouble());
                    if (alertId.isEmpty())
                        alertId = QString::number(qHash(title + QString::number(time)));
                    QVariantMap alert;
                    alert.insert(QStringLiteral("alertId"), alertId);
                    alert.insert(QStringLiteral("type"), title);
                    alert.insert(QStringLiteral("level"), cwn::weather::alertLevelLetter(title));
                    alert.insert(QStringLiteral("levelRaw"), title);
                    alert.insert(QStringLiteral("title"), title);
                    alert.insert(QStringLiteral("pubTime"),
                                 time > 0
                                     ? QDateTime::fromSecsSinceEpoch(time).toString(
                                         Qt::ISODate)
                                     : QString());
                    alerts.append(alert);
                }

                WeatherProvider::Snapshot snapshot;
                snapshot.valid = !current.isEmpty() || !today.isEmpty();
                snapshot.current = current;
                snapshot.today = today;
                snapshot.alerts = alerts;
                emit fetchFinished(cityId, snapshot, QString());
            });
}

void CaiyunProvider::testConnection()
{
    // 用北京坐标作探针（realtime 端点）
    QUrl url(QStringLiteral("https://api.caiyunapp.com/v2.6/") + token()
             + QStringLiteral("/116.4,39.9/realtime"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("unit"), QStringLiteral("metric"));
    query.addQueryItem(QStringLiteral("lang"), QStringLiteral("zh_CN"));
    url.setQuery(query);
    fetchUrl(url, QString(), /*test=*/true);
}
