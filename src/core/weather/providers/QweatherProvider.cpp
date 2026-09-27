#include "QweatherProvider.h"

#include "../../ConfigStore.h"
#include "../WeatherCodes.h"

#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QUrlQuery>

QweatherProvider::QweatherProvider(ConfigStore *configs, QNetworkAccessManager *nam,
                                   QObject *parent)
    : WeatherProvider(nam, parent)
    , m_configs(configs)
{
}

QString QweatherProvider::key() const
{
    if (m_configs) {
        if (const auto v = m_configs->value(QStringLiteral("weather.keys.qweather.key")))
            return v->toString().trimmed();
    }
    return {};
}

QString QweatherProvider::host() const
{
    QString hostValue;
    if (m_configs) {
        if (const auto v = m_configs->value(QStringLiteral("weather.keys.qweather.host")))
            hostValue = v->toString().trimmed();
    }
    hostValue.remove(QLatin1String("https://"));
    hostValue.remove(QLatin1String("http://"));
    while (hostValue.endsWith(QLatin1Char('/')))
        hostValue.chop(1);
    return hostValue;
}

bool QweatherProvider::isConfigured() const
{
    return !key().isEmpty() && !host().isEmpty();
}

QUrl QweatherProvider::apiUrl(const QString &path, const QUrlQuery &query) const
{
    QUrl url(QStringLiteral("https://") + host() + path);
    url.setQuery(query);
    return url;
}

std::list<std::pair<QString, QString>> QweatherProvider::authHeader() const
{
    return { { QStringLiteral("X-QW-Api-Key"), key() } };
}

QString QweatherProvider::errorKindForCode(const QString &code)
{
    if (code == QLatin1String("401") || code == QLatin1String("403"))
        return QStringLiteral("auth");
    if (code == QLatin1String("402") || code == QLatin1String("429"))
        return QStringLiteral("quota");
    return QStringLiteral("network");
}

void QweatherProvider::searchCity(const QString &keyword)
{
    const QString trimmed = keyword.trimmed();
    if (trimmed.isEmpty()) {
        emit citySearchFinished({});
        return;
    }

    // GeoAPI 城市搜索：location 支持名称；响应数值字段全是字符串
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("location"), trimmed);
    query.addQueryItem(QStringLiteral("number"), QStringLiteral("6"));
    query.addQueryItem(QStringLiteral("lang"), QStringLiteral("zh"));
    getJson(apiUrl(QStringLiteral("/geo/v2/city/lookup"), query), authHeader(),
            cwn::weather::kSearchTimeoutMs,
            [this](const QJsonDocument &doc, const QString &errorKind, int) {
                QVariantList cities;
                if (errorKind.isEmpty()
                    && doc.object().value(QLatin1String("code")).toString()
                        == QLatin1String("200")) {
                    for (const QJsonValue &item :
                         doc.object().value(QLatin1String("location")).toArray()) {
                        const QJsonObject obj = item.toObject();
                        bool latOk = false;
                        bool lonOk = false;
                        const double lat = obj.value(QLatin1String("lat")).toString().toDouble(&latOk);
                        const double lon = obj.value(QLatin1String("lon")).toString().toDouble(&lonOk);
                        const QString id = obj.value(QLatin1String("id")).toString();
                        const QString name = obj.value(QLatin1String("name")).toString();
                        if (id.isEmpty() || name.isEmpty() || !latOk || !lonOk)
                            continue;
                        QVariantMap city;
                        city.insert(QStringLiteral("cityId"), id);
                        city.insert(QStringLiteral("name"), name);
                        city.insert(QStringLiteral("lat"), lat);
                        city.insert(QStringLiteral("lon"), lon);
                        city.insert(QStringLiteral("province"),
                                    obj.value(QLatin1String("adm1")).toString());
                        cities.append(city);
                    }
                }
                emit citySearchFinished(cities);
            });
}

void QweatherProvider::fetch(const CityInfo &city)
{
    if (m_pending.contains(city.cityId))
        return;
    m_pending.insert(city.cityId, Pending{ {}, {}, {}, 3 });

    // 经纬度最多两位小数（接口约束）；localTime=true 使日出日落为本地时间
    const QString latLon = QStringLiteral("%1/%2").arg(QString::number(city.latitude, 'f', 2),
                                                       QString::number(city.longitude, 'f', 2));

    auto start = [this, cityId = city.cityId](int which, const QString &path,
                                              const QUrlQuery &query) {
        getJson(apiUrl(path, query), authHeader(), cwn::weather::kFetchTimeoutMs,
                [this, cityId, which](const QJsonDocument &doc, const QString &errorKind, int) {
                    handleFetchResponse(cityId, which, doc, errorKind);
                });
    };

    QUrlQuery currentQuery;
    currentQuery.addQueryItem(QStringLiteral("lang"), QStringLiteral("zh"));
    currentQuery.addQueryItem(QStringLiteral("localTime"), QStringLiteral("true"));
    start(0, QStringLiteral("/weather/v1/current/") + latLon, currentQuery);

    QUrlQuery dailyQuery;
    dailyQuery.addQueryItem(QStringLiteral("days"), QStringLiteral("3"));
    dailyQuery.addQueryItem(QStringLiteral("lang"), QStringLiteral("zh"));
    dailyQuery.addQueryItem(QStringLiteral("localTime"), QStringLiteral("true"));
    start(1, QStringLiteral("/weather/v1/daily/") + latLon, dailyQuery);

    QUrlQuery alertQuery;
    alertQuery.addQueryItem(QStringLiteral("lang"), QStringLiteral("zh"));
    start(2, QStringLiteral("/weatheralert/v1/current/") + latLon, alertQuery);
}

void QweatherProvider::handleFetchResponse(const QString &cityId, int which,
                                           const QJsonDocument &doc,
                                           const QString &transportKind)
{
    auto it = m_pending.find(cityId);
    if (it == m_pending.end())
        return;
    QString kind = transportKind;
    if (kind.isEmpty()) {
        const QString code = doc.object().value(QLatin1String("code")).toString();
        if (!code.isEmpty() && code != QLatin1String("200"))
            kind = errorKindForCode(code);
    }
    if (!kind.isEmpty() && which != 2) {
        // 实况/预报失败终结本轮；预警失败降级（alerts 留空）
        m_pending.erase(it);
        emit fetchFinished(cityId, {}, kind);
        return;
    }

    const QJsonObject body = doc.object();
    if (which == 0) {
        // current：condition.{text,code} + temperature/feelsLike {value,unit}；
        // humidity 0-1 比率 → ×100；wind.speed.value 已是 m/s
        QVariantMap current;
        bool codeOk = false;
        double temperature = 0;
        const bool tempOk = cwn::weather::readDouble(
            body.value(QLatin1String("temperature")).toObject().value(QLatin1String("value")),
            temperature);
        const int qcode = body.value(QLatin1String("condition"))
                              .toObject()
                              .value(QLatin1String("code"))
                              .toString()
                              .toInt(&codeOk);
        if (tempOk && codeOk) {
            current.insert(QStringLiteral("temperature"), temperature);
            current.insert(QStringLiteral("weatherCode"), cwn::weather::codeFromQweatherCode(qcode));
            double feelsLike = 0;
            const bool feelsOk = cwn::weather::readDouble(
                body.value(QLatin1String("feelsLike")).toObject().value(QLatin1String("value")),
                feelsLike);
            if (feelsOk)
                current.insert(QStringLiteral("feelsLike"), feelsLike);
            double humidity = 0;
            const bool humidityOk =
                cwn::weather::readDouble(body.value(QLatin1String("humidity")), humidity);
            if (humidityOk)
                current.insert(QStringLiteral("humidity"),
                               qBound(0.0, humidity * 100.0, 100.0));
            double windSpeed = 0;
            const bool windOk = cwn::weather::readDouble(
                body.value(QLatin1String("wind")).toObject().value(QLatin1String("speed"))
                    .toObject()
                    .value(QLatin1String("value")),
                windSpeed);
            if (windOk)
                current.insert(QStringLiteral("windSpeed"), windSpeed);
            it->current = current;
        }
    } else if (which == 1) {
        // daily：days[0] 温度极值 + 昼/夜现象码 + 降水概率（0-1 → ×100）
        // + astro 日出日落（localTime=true 时为本地 ISO，取第 11-15 字符）
        const QJsonArray days = body.value(QLatin1String("days")).toArray();
        if (!days.isEmpty()) {
            const QJsonObject day0 = days.at(0).toObject();
            const QJsonObject daytime = day0.value(QLatin1String("daytime")).toObject();
            const QJsonObject nighttime = day0.value(QLatin1String("nighttime")).toObject();
            bool dayOk = false;
            bool nightOk = false;
            bool probOk = false;
            double tempMax = 0;
            const bool maxOk = cwn::weather::readDouble(
                day0.value(QLatin1String("temperatureMax"))
                    .toObject()
                    .value(QLatin1String("value")),
                tempMax);
            double tempMin = 0;
            const bool minOk = cwn::weather::readDouble(
                day0.value(QLatin1String("temperatureMin"))
                    .toObject()
                    .value(QLatin1String("value")),
                tempMin);
            const int dayCode = cwn::weather::codeFromQweatherCode(
                daytime.value(QLatin1String("condition"))
                    .toObject()
                    .value(QLatin1String("code"))
                    .toString()
                    .toInt(&dayOk));
            const int nightCode = cwn::weather::codeFromQweatherCode(
                nighttime.value(QLatin1String("condition"))
                    .toObject()
                    .value(QLatin1String("code"))
                    .toString()
                    .toInt(&nightOk));
            double probability = 0;
            probOk = cwn::weather::readDouble(
                daytime.value(QLatin1String("precipitation"))
                    .toObject()
                    .value(QLatin1String("probability")),
                probability);
            QVariantMap today;
            if (maxOk)
                today.insert(QStringLiteral("tempMax"), tempMax);
            if (minOk)
                today.insert(QStringLiteral("tempMin"), tempMin);
            if (dayOk)
                today.insert(QStringLiteral("dayCode"), dayCode);
            if (nightOk)
                today.insert(QStringLiteral("nightCode"), nightCode);
            if (probOk)
                today.insert(QStringLiteral("precipProb"),
                             qBound(0.0, probability * 100.0, 100.0));
            const QString sunrise =
                day0.value(QLatin1String("astro")).toObject().value(QLatin1String("sunrise"))
                    .toString()
                    .mid(11, 5);
            const QString sunset =
                day0.value(QLatin1String("astro")).toObject().value(QLatin1String("sunset"))
                    .toString()
                    .mid(11, 5);
            if (!sunrise.isEmpty())
                today.insert(QStringLiteral("sunrise"), sunrise);
            if (!sunset.isEmpty())
                today.insert(QStringLiteral("sunset"), sunset);
            it->today = today;
        }
    } else {
        // alert：severity/color.code 归一 B/Y/O/R，原文另存 levelRaw
        QVariantList alerts;
        for (const QJsonValue &item : body.value(QLatin1String("alerts")).toArray()) {
            const QJsonObject obj = item.toObject();
            const QString id = obj.value(QLatin1String("id")).toString();
            const QString title = obj.value(QLatin1String("headline")).toString();
            if (id.isEmpty() || title.isEmpty())
                continue;
            const QString colorCode = obj.value(QLatin1String("color"))
                                          .toObject()
                                          .value(QLatin1String("code"))
                                          .toString();
            QVariantMap alert;
            alert.insert(QStringLiteral("alertId"), id);
            alert.insert(QStringLiteral("type"),
                         obj.value(QLatin1String("eventType"))
                             .toObject()
                             .value(QLatin1String("name"))
                             .toString());
            alert.insert(QStringLiteral("level"), cwn::weather::alertLevelLetter(colorCode));
            alert.insert(QStringLiteral("levelRaw"), colorCode);
            alert.insert(QStringLiteral("title"), title);
            alert.insert(QStringLiteral("pubTime"),
                         obj.value(QLatin1String("issuedTime")).toString());
            alerts.append(alert);
        }
        it->alerts = alerts;
    }

    if (--it->remaining <= 0) {
        WeatherProvider::Snapshot snapshot;
        snapshot.valid = !it->current.isEmpty() || !it->today.isEmpty();
        snapshot.current = it->current;
        snapshot.today = it->today;
        snapshot.alerts = it->alerts;
        m_pending.erase(it);
        emit fetchFinished(cityId, snapshot, QString());
    }
}

void QweatherProvider::testConnection()
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("location"), QStringLiteral("北京"));
    query.addQueryItem(QStringLiteral("number"), QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("lang"), QStringLiteral("zh"));
    getJson(apiUrl(QStringLiteral("/geo/v2/city/lookup"), query), authHeader(),
            cwn::weather::kSearchTimeoutMs,
            [this](const QJsonDocument &doc, const QString &errorKind, int) {
                if (!errorKind.isEmpty()) {
                    emit connectionTestFinished(false, errorKind);
                    return;
                }
                const QString code = doc.object().value(QLatin1String("code")).toString();
                const bool ok = code == QLatin1String("200");
                emit connectionTestFinished(ok, ok ? QString() : errorKindForCode(code));
            });
}
