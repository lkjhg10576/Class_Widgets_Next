#include "XiaomiProvider.h"

#include <QDateTime>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QUrlQuery>

namespace {

// 数据源常量（weather.rs:26-28 原文；无动态签名，固定常量）
constexpr auto kBaseUrl = "https://weatherapi.market.xiaomi.com/wtr-v3";
constexpr auto kAppKey = "weather20151024";
constexpr auto kSign = "zUFJoAR2ZVrDy1vF3D07";

qint64 unixNow()
{
    return QDateTime::currentSecsSinceEpoch();
}

// weather/all 响应解析（自 WeatherService.cpp applySnapshot 平移，逻辑不变）。
// 小米接口数值字段全部是字符串编码（"value": "22"），一律 string → number 解析；
// 必填字段缺失/不可解析时整块丢弃（上游 None 静默降级语义）。
WeatherProvider::Snapshot parseWeatherAll(const QJsonObject &body)
{
    WeatherProvider::Snapshot snapshot;

    const QJsonObject currentObj = body.value(QLatin1String("current")).toObject();
    bool tempOk = false;
    bool codeOk = false;
    bool feelsOk = false;
    bool humidityOk = false;
    bool windOk = false;
    const double temperature =
        currentObj.value(QLatin1String("temperature")).toObject().value(QLatin1String("value"))
            .toString()
            .toDouble(&tempOk);
    const int weatherCode = currentObj.value(QLatin1String("weather")).toString().toInt(&codeOk);
    const double feelsLike =
        currentObj.value(QLatin1String("feelsLike")).toObject().value(QLatin1String("value"))
            .toString()
            .toDouble(&feelsOk);
    const double humidity =
        currentObj.value(QLatin1String("humidity")).toObject().value(QLatin1String("value"))
            .toString()
            .toDouble(&humidityOk);
    const double windSpeed = currentObj.value(QLatin1String("wind"))
                                 .toObject()
                                 .value(QLatin1String("speed"))
                                 .toObject()
                                 .value(QLatin1String("value"))
                                 .toString()
                                 .toDouble(&windOk);

    QVariantMap current;
    if (tempOk && codeOk && feelsOk && humidityOk && windOk) {
        current.insert(QStringLiteral("temperature"), temperature);
        current.insert(QStringLiteral("weatherCode"), weatherCode);
        current.insert(QStringLiteral("feelsLike"), feelsLike);
        current.insert(QStringLiteral("humidity"), humidity);
        current.insert(QStringLiteral("windSpeed"), windSpeed);
        // PM2.5 / AQI 在根级 aqi 对象，不在 current 里；缺失仅置空该字段
        const QJsonObject aqiObj = body.value(QLatin1String("aqi")).toObject();
        const auto readOptional = [](const QJsonValue &value) {
            bool ok = false;
            const double parsed = value.toString().toDouble(&ok);
            return ok ? QVariant(parsed) : QVariant();
        };
        const QVariant pm25 = readOptional(aqiObj.value(QLatin1String("pm25")));
        const QVariant aqi = readOptional(aqiObj.value(QLatin1String("aqi")));
        if (!pm25.isNull())
            current.insert(QStringLiteral("pm25"), pm25);
        if (!aqi.isNull())
            current.insert(QStringLiteral("aqi"), aqi);
    }

    // 逐日预报是"列式"数组：每个属性的 value 按天排列，索引 0 = 今天
    QVariantMap today;
    const QJsonObject daily = body.value(QLatin1String("forecastDaily")).toObject();
    const QJsonArray tempVals =
        daily.value(QLatin1String("temperature")).toObject().value(QLatin1String("value")).toArray();
    const QJsonArray weatherVals =
        daily.value(QLatin1String("weather")).toObject().value(QLatin1String("value")).toArray();
    const QJsonArray precipVals = daily.value(QLatin1String("precipitationProbability"))
                                      .toObject()
                                      .value(QLatin1String("value"))
                                      .toArray();
    const QJsonArray sunVals =
        daily.value(QLatin1String("sunRiseSet")).toObject().value(QLatin1String("value")).toArray();
    if (!tempVals.isEmpty() && !weatherVals.isEmpty() && !precipVals.isEmpty()) {
        bool maxOk = false;
        bool minOk = false;
        bool dayOk = false;
        bool nightOk = false;
        bool precipOk = false;
        const double tempMax =
            tempVals.at(0).toObject().value(QLatin1String("from")).toString().toDouble(&maxOk);
        const double tempMin =
            tempVals.at(0).toObject().value(QLatin1String("to")).toString().toDouble(&minOk);
        const int dayCode =
            weatherVals.at(0).toObject().value(QLatin1String("from")).toString().toInt(&dayOk);
        const int nightCode =
            weatherVals.at(0).toObject().value(QLatin1String("to")).toString().toInt(&nightOk);
        const double precipProb = precipVals.at(0).toString().toDouble(&precipOk);
        if (maxOk && minOk && dayOk && nightOk && precipOk) {
            today.insert(QStringLiteral("tempMax"), tempMax);
            today.insert(QStringLiteral("tempMin"), tempMin);
            today.insert(QStringLiteral("dayCode"), dayCode);
            today.insert(QStringLiteral("nightCode"), nightCode);
            today.insert(QStringLiteral("precipProb"), precipProb);
            // 日出/日落：接口返回完整 ISO 时间（"2026-09-15T05:55:00+08:00"），
            // 取第 11-15 字符得 "HH:MM"（格式不符时 mid 返回空串，丢弃该字段）
            const QString sunrise =
                sunVals.at(0).toObject().value(QLatin1String("from")).toString().mid(11, 5);
            const QString sunset =
                sunVals.at(0).toObject().value(QLatin1String("to")).toString().mid(11, 5);
            if (!sunrise.isEmpty())
                today.insert(QStringLiteral("sunrise"), sunrise);
            if (!sunset.isEmpty())
                today.insert(QStringLiteral("sunset"), sunset);
        }
    }

    // 预警：level 原样透传（B/Y/O/R 或"蓝色"等中文），文案与配色归一在服务层
    QVariantList alerts;
    const QJsonArray alertArr = body.value(QLatin1String("alerts")).toArray();
    for (const QJsonValue &item : alertArr) {
        const QJsonObject obj = item.toObject();
        const QString alertId = obj.value(QLatin1String("alertId")).toString();
        const QString typeName = obj.value(QLatin1String("type")).toString();
        if (alertId.isEmpty() || typeName.isEmpty())
            continue;
        const QString rawLevel = obj.value(QLatin1String("level")).toString();
        QVariantMap alert;
        alert.insert(QStringLiteral("alertId"), alertId);
        alert.insert(QStringLiteral("type"), typeName);
        alert.insert(QStringLiteral("level"), rawLevel);
        alert.insert(QStringLiteral("levelRaw"), rawLevel);
        alert.insert(QStringLiteral("title"), obj.value(QLatin1String("title")).toString());
        alert.insert(QStringLiteral("pubTime"), obj.value(QLatin1String("pubTime")).toString());
        alerts.append(alert);
    }

    snapshot.valid = true;
    snapshot.current = current;
    snapshot.today = today;
    snapshot.alerts = alerts;
    return snapshot;
}

} // namespace

XiaomiProvider::XiaomiProvider(QNetworkAccessManager *nam, QObject *parent)
    : WeatherProvider(nam, parent)
{
}

void XiaomiProvider::searchCity(const QString &keyword)
{
    const QString trimmed = keyword.trimmed();
    if (trimmed.isEmpty()) {
        emit citySearchFinished({});
        return;
    }

    // location/city/search（weather.rs:805-813）：无鉴权参数，超时 5s
    QUrl url(QLatin1String(kBaseUrl) + QStringLiteral("/location/city/search"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("name"), trimmed); // addQueryItem 自动百分号编码
    query.addQueryItem(QStringLiteral("locale"), QStringLiteral("zh_cn"));
    url.setQuery(query);

    getJson(url, {}, cwn::weather::kSearchTimeoutMs,
            [this](const QJsonDocument &doc, const QString &errorKind, int) {
                QVariantList cities;
                if (errorKind.isEmpty()) {
                    // 响应顶层是 JSON 数组；latitude/longitude 为字符串编码
                    for (const QJsonValue &item : doc.array()) {
                        const QJsonObject obj = item.toObject();
                        QString cityId = obj.value(QLatin1String("locationKey")).toString();
                        const QString name = obj.value(QLatin1String("name")).toString();
                        bool latOk = false;
                        bool lonOk = false;
                        const double lat =
                            obj.value(QLatin1String("latitude")).toString().toDouble(&latOk);
                        const double lon =
                            obj.value(QLatin1String("longitude")).toString().toDouble(&lonOk);
                        // locationKey 形如 weathercn:101010100 → 剥前缀作 cityId
                        if (cityId.startsWith(QLatin1String("weathercn:")))
                            cityId.remove(0, static_cast<int>(qstrlen("weathercn:")));
                        if (cityId.isEmpty() || name.isEmpty() || !latOk || !lonOk)
                            continue;

                        QVariantMap city;
                        city.insert(QStringLiteral("cityId"), cityId);
                        city.insert(QStringLiteral("name"), name);
                        city.insert(QStringLiteral("lat"), lat);
                        city.insert(QStringLiteral("lon"), lon);
                        city.insert(QStringLiteral("province"),
                                    obj.value(QLatin1String("affiliation")).toString());
                        cities.append(city);
                    }
                }
                emit citySearchFinished(cities);
            });
}

void XiaomiProvider::fetch(const CityInfo &city)
{
    // 合并接口（weather.rs:330-340）：实况 + 15 日预报 + 预警一次拉回
    QUrl url(QLatin1String(kBaseUrl) + QStringLiteral("/weather/all"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("latitude"), QString::number(city.latitude, 'g', 12));
    query.addQueryItem(QStringLiteral("longitude"), QString::number(city.longitude, 'g', 12));
    query.addQueryItem(QStringLiteral("locationKey"),
                       QStringLiteral("weathercn:") + city.cityId);
    query.addQueryItem(QStringLiteral("days"), QStringLiteral("15"));
    query.addQueryItem(QStringLiteral("appKey"), QLatin1String(kAppKey));
    query.addQueryItem(QStringLiteral("sign"), QLatin1String(kSign));
    query.addQueryItem(QStringLiteral("isGlobal"), QStringLiteral("false"));
    query.addQueryItem(QStringLiteral("locale"), QStringLiteral("zh_cn"));
    query.addQueryItem(QStringLiteral("ts"), QString::number(unixNow()));
    url.setQuery(query);

    getJson(url, {}, cwn::weather::kFetchTimeoutMs,
            [this, cityId = city.cityId](const QJsonDocument &doc, const QString &errorKind, int) {
                if (!errorKind.isEmpty()) {
                    emit fetchFinished(cityId, {}, errorKind);
                    return;
                }
                emit fetchFinished(cityId, parseWeatherAll(doc.object()), errorKind);
            });
}

void XiaomiProvider::testConnection()
{
    // 免配置源：以北京作探针验证网络可达与响应可解析
    QUrl url(QLatin1String(kBaseUrl) + QStringLiteral("/weather/all"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("latitude"), QStringLiteral("39.9"));
    query.addQueryItem(QStringLiteral("longitude"), QStringLiteral("116.4"));
    query.addQueryItem(QStringLiteral("locationKey"), QStringLiteral("weathercn:101010100"));
    query.addQueryItem(QStringLiteral("days"), QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("appKey"), QLatin1String(kAppKey));
    query.addQueryItem(QStringLiteral("sign"), QLatin1String(kSign));
    query.addQueryItem(QStringLiteral("isGlobal"), QStringLiteral("false"));
    query.addQueryItem(QStringLiteral("locale"), QStringLiteral("zh_cn"));
    query.addQueryItem(QStringLiteral("ts"), QString::number(unixNow()));
    url.setQuery(query);

    getJson(url, {}, cwn::weather::kFetchTimeoutMs,
            [this](const QJsonDocument &doc, const QString &errorKind, int) {
                if (!errorKind.isEmpty()) {
                    emit connectionTestFinished(false, errorKind);
                    return;
                }
                const bool ok = !parseWeatherAll(doc.object()).current.isEmpty();
                emit connectionTestFinished(ok, ok ? QString() : QStringLiteral("parse"));
            });
}
