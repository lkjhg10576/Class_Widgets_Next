#include "WeathercnProvider.h"

#include "../../ConfigStore.h"
#include "../WeatherCodes.h"

#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QUrlQuery>

namespace {

constexpr auto kBase = "https://openapi.weathercn.com";

} // namespace

WeathercnProvider::WeathercnProvider(ConfigStore *configs, QNetworkAccessManager *nam,
                                     QObject *parent)
    : WeatherProvider(nam, parent)
    , m_configs(configs)
{
}

QString WeathercnProvider::key() const
{
    if (m_configs) {
        if (const auto v = m_configs->value(QStringLiteral("weather.keys.weathercn.key")))
            return v->toString().trimmed();
    }
    return {};
}

bool WeathercnProvider::isConfigured() const
{
    return !key().isEmpty();
}

QUrl WeathercnProvider::apiUrl(const QString &path, const QUrlQuery &query) const
{
    // 鉴权只能走 URL 参数 apikey：Kong 网关层认 apikey 头，但实测后端业务
    // 不读该头（400 "Apikey invalid."），仅查询串里的 apikey 能通到业务层
    // （文档所写 X-Gw-API-Key 头则连网关都不认，401）。
    QUrl url(QLatin1String(kBase) + path);
    QUrlQuery full(query);
    full.addQueryItem(QStringLiteral("apikey"), key());
    url.setQuery(full);
    return url;
}

void WeathercnProvider::searchCity(const QString &keyword)
{
    const QString trimmed = keyword.trimmed();
    if (trimmed.isEmpty()) {
        emit citySearchFinished({});
        return;
    }

    // 文字搜索：q=城市名；响应为数组（兼容单对象），Key 即 Location Key
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("q"), trimmed);
    query.addQueryItem(QStringLiteral("language"), QStringLiteral("zh-cn"));
    getJson(apiUrl(QStringLiteral("/locations/v1/cities/translate"), query), {},
            cwn::weather::kSearchTimeoutMs,
            [this](const QJsonDocument &doc, const QString &errorKind, int) {
                QVariantList cities;
                if (errorKind.isEmpty()) {
                    QJsonArray items = doc.array();
                    if (items.isEmpty() && doc.object().contains(QLatin1String("Key")))
                        items.append(doc.object());
                    for (const QJsonValue &item : items) {
                        const QJsonObject obj = item.toObject();
                        const QString locationKey = obj.value(QLatin1String("Key")).toString();
                        const QString name =
                            obj.value(QLatin1String("LocalizedName")).toString();
                        if (locationKey.isEmpty() || name.isEmpty())
                            continue;
                        QVariantMap city;
                        city.insert(QStringLiteral("cityId"), locationKey);
                        city.insert(QStringLiteral("name"), name);
                        city.insert(
                            QStringLiteral("province"),
                            obj.value(QLatin1String("AdministrativeArea"))
                                .toObject()
                                .value(QLatin1String("LocalizedName"))
                                .toString());
                        city.insert(QStringLiteral("lat"),
                                    obj.value(QLatin1String("GeoPosition"))
                                        .toObject()
                                        .value(QLatin1String("Latitude"))
                                        .toDouble());
                        city.insert(QStringLiteral("lon"),
                                    obj.value(QLatin1String("GeoPosition"))
                                        .toObject()
                                        .value(QLatin1String("Longitude"))
                                        .toDouble());
                        city.insert(QStringLiteral("wcnKey"), locationKey);
                        cities.append(city);
                    }
                }
                emit citySearchFinished(cities);
            });
}

void WeathercnProvider::fetch(const CityInfo &city)
{
    const QString cached = city.wcnKey.isEmpty() ? m_keyCache.value(city.cityId) : city.wcnKey;
    if (!cached.isEmpty()) {
        fetchWeather(city, cached);
        return;
    }
    resolveKeyAndFetch(city);
}

void WeathercnProvider::resolveKeyAndFetch(const CityInfo &city)
{
    // 坐标定位：q="纬度,经度"（3 位小数），返回单个对象含 Key（如朝阳区 57456）
    QUrlQuery query;
    query.addQueryItem(
        QStringLiteral("q"),
        QStringLiteral("%1,%2").arg(QString::number(city.latitude, 'f', 3),
                                    QString::number(city.longitude, 'f', 3)));
    query.addQueryItem(QStringLiteral("language"), QStringLiteral("zh-cn"));
    getJson(apiUrl(QStringLiteral("/locations/v1/cities/geoposition/search.json"), query),
            {}, cwn::weather::kSearchTimeoutMs,
            [this, city](const QJsonDocument &doc, const QString &errorKind, int) {
                QString kind = errorKind;
                QString locationKey;
                if (kind.isEmpty()) {
                    locationKey = doc.object().value(QLatin1String("Key")).toString();
                    if (locationKey.isEmpty())
                        kind = QStringLiteral("parse");
                }
                if (!kind.isEmpty()) {
                    emit fetchFinished(city.cityId, {}, kind);
                    return;
                }
                m_keyCache.insert(city.cityId, locationKey);
                fetchWeather(city, locationKey);
            });
}

void WeathercnProvider::fetchWeather(const CityInfo &city, const QString &locationKey)
{
    if (m_pending.contains(city.cityId))
        return;
    m_pending.insert(city.cityId, Pending{ {}, {}, {}, 3 });

    auto start = [this, cityId = city.cityId](int which, const QString &path) {
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("language"), QStringLiteral("zh-cn"));
        if (which != 2)
            query.addQueryItem(QStringLiteral("details"), QStringLiteral("true"));
        if (which == 1)
            query.addQueryItem(QStringLiteral("metric"), QStringLiteral("true"));
        getJson(apiUrl(path, query), {}, cwn::weather::kFetchTimeoutMs,
                [this, cityId, which](const QJsonDocument &doc, const QString &errorKind, int) {
                    handleFetchResponse(cityId, which, doc, errorKind);
                });
    };
    start(0, QStringLiteral("/currentconditions/v1/%1.json").arg(locationKey));
    start(1, QStringLiteral("/forecasts/v1/daily/5day/%1.json").arg(locationKey));
    start(2, QStringLiteral("/alerts/v1/%1.json").arg(locationKey));
}

void WeathercnProvider::handleFetchResponse(const QString &cityId, int which,
                                            const QJsonDocument &doc,
                                            const QString &transportKind)
{
    auto it = m_pending.find(cityId);
    if (it == m_pending.end())
        return;
    if (!transportKind.isEmpty() && which != 2) {
        // 预警失败降级（alerts 留空）；实况/预报失败终结本轮
        m_pending.erase(it);
        emit fetchFinished(cityId, {}, transportKind);
        return;
    }

    if (which == 0) {
        // 实况：响应是数组；温度取 Metric，风速 km/h ÷ 3.6 → m/s
        const QJsonArray items = doc.array();
        if (!items.isEmpty()) {
            const QJsonObject obj = items.at(0).toObject();
            QVariantMap current;
            double temperature = 0;
            const bool tempOk = cwn::weather::readDouble(
                obj.value(QLatin1String("Temperature"))
                    .toObject()
                    .value(QLatin1String("Metric"))
                    .toObject()
                    .value(QLatin1String("Value")),
                temperature);
            const int weatherIcon = obj.value(QLatin1String("WeatherIcon")).toInt(-1);
            if (tempOk && weatherIcon >= 0) {
                current.insert(QStringLiteral("temperature"), temperature);
                current.insert(QStringLiteral("weatherCode"),
                               cwn::weather::codeFromWeathercnIcon(weatherIcon));
                double feelsLike = 0;
                const bool feelsOk = cwn::weather::readDouble(
                    obj.value(QLatin1String("RealFeelTemperature"))
                        .toObject()
                        .value(QLatin1String("Metric"))
                        .toObject()
                        .value(QLatin1String("Value")),
                    feelsLike);
                if (feelsOk)
                    current.insert(QStringLiteral("feelsLike"), feelsLike);
                if (obj.contains(QLatin1String("RelativeHumidity"))) {
                    current.insert(QStringLiteral("humidity"),
                                   qBound(0.0,
                                          obj.value(QLatin1String("RelativeHumidity")).toDouble(),
                                          100.0));
                }
                double windKmh = 0;
                const bool windOk = cwn::weather::readDouble(
                    obj.value(QLatin1String("Wind"))
                        .toObject()
                        .value(QLatin1String("Speed"))
                        .toObject()
                        .value(QLatin1String("Metric"))
                        .toObject()
                        .value(QLatin1String("Value")),
                    windKmh);
                if (windOk)
                    current.insert(QStringLiteral("windSpeed"),
                                   qRound(windKmh / 3.6 * 10.0) / 10.0);
            }
            it->current = current;
        }
    } else if (which == 1) {
        // 逐日：DailyForecasts[0] 温度极值 + 昼/夜图标号 + 降水概率（0-100）
        // + Sun.Rise/Set（本地 ISO，取第 11-15 字符得 "HH:MM"）
        const QJsonArray forecasts =
            doc.object().value(QLatin1String("DailyForecasts")).toArray();
        if (!forecasts.isEmpty()) {
            const QJsonObject day0 = forecasts.at(0).toObject();
            const QJsonObject day = day0.value(QLatin1String("Day")).toObject();
            const QJsonObject night = day0.value(QLatin1String("Night")).toObject();
            QVariantMap today;
            double tempMax = 0;
            const bool maxOk = cwn::weather::readDouble(
                day0.value(QLatin1String("Temperature"))
                    .toObject()
                    .value(QLatin1String("Maximum"))
                    .toObject()
                    .value(QLatin1String("Value")),
                tempMax);
            double tempMin = 0;
            const bool minOk = cwn::weather::readDouble(
                day0.value(QLatin1String("Temperature"))
                    .toObject()
                    .value(QLatin1String("Minimum"))
                    .toObject()
                    .value(QLatin1String("Value")),
                tempMin);
            if (maxOk)
                today.insert(QStringLiteral("tempMax"), tempMax);
            if (minOk)
                today.insert(QStringLiteral("tempMin"), tempMin);
            const int dayIcon = day.value(QLatin1String("Icon")).toInt(-1);
            const int nightIcon = night.value(QLatin1String("Icon")).toInt(-1);
            if (dayIcon >= 0)
                today.insert(QStringLiteral("dayCode"), cwn::weather::codeFromWeathercnIcon(dayIcon));
            if (nightIcon >= 0)
                today.insert(QStringLiteral("nightCode"),
                             cwn::weather::codeFromWeathercnIcon(nightIcon));
            if (day.contains(QLatin1String("PrecipitationProbability")))
                today.insert(QStringLiteral("precipProb"),
                             qBound(0.0, day.value(QLatin1String("PrecipitationProbability"))
                                             .toDouble(),
                                    100.0));
            const QString sunrise = day0.value(QLatin1String("Sun"))
                                        .toObject()
                                        .value(QLatin1String("Rise"))
                                        .toString()
                                        .mid(11, 5);
            const QString sunset = day0.value(QLatin1String("Sun"))
                                       .toObject()
                                       .value(QLatin1String("Set"))
                                       .toString()
                                       .mid(11, 5);
            if (!sunrise.isEmpty())
                today.insert(QStringLiteral("sunrise"), sunrise);
            if (!sunset.isEmpty())
                today.insert(QStringLiteral("sunset"), sunset);
            it->today = today;
        }
    } else {
        // 预警：响应是数组；Level 中文（"红色"）/AlarmLevel（"Red"）归一 B/Y/O/R
        QVariantList alerts;
        for (const QJsonValue &item : doc.array()) {
            const QJsonObject obj = item.toObject();
            const int alertIdNum = obj.value(QLatin1String("AlertID")).toInt(-1);
            if (alertIdNum < 0)
                continue;
            const QString alarmLevel = obj.value(QLatin1String("AlarmLevel")).toString();
            const QString levelChinese = obj.value(QLatin1String("Level")).toString();
            const QJsonArray areas = obj.value(QLatin1String("Area")).toArray();
            QVariantMap alert;
            alert.insert(QStringLiteral("alertId"), QString::number(alertIdNum));
            alert.insert(QStringLiteral("type"), obj.value(QLatin1String("Type")).toString());
            alert.insert(QStringLiteral("level"),
                         cwn::weather::alertLevelLetter(alarmLevel.isEmpty() ? levelChinese
                                                                             : alarmLevel));
            alert.insert(QStringLiteral("levelRaw"), levelChinese);
            alert.insert(QStringLiteral("title"),
                         obj.value(QLatin1String("Description"))
                             .toObject()
                             .value(QLatin1String("Localized"))
                             .toString());
            alert.insert(QStringLiteral("pubTime"),
                         areas.isEmpty()
                             ? QString()
                             : areas.at(0).toObject().value(QLatin1String("StartTime")).toString());
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

void WeathercnProvider::testConnection()
{
    // 用北京 Location Key（101924）作探针
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("language"), QStringLiteral("zh-cn"));
    getJson(apiUrl(QStringLiteral("/currentconditions/v1/101924.json"), query), {},
            cwn::weather::kFetchTimeoutMs,
            [this](const QJsonDocument &doc, const QString &errorKind, int) {
                if (!errorKind.isEmpty()) {
                    emit connectionTestFinished(false, errorKind);
                    return;
                }
                const bool ok = doc.isArray() && !doc.array().isEmpty();
                emit connectionTestFinished(ok, ok ? QString() : QStringLiteral("parse"));
            });
}
