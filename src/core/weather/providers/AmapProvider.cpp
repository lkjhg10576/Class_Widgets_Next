#include "AmapProvider.h"

#include "../../ConfigStore.h"
#include "../WeatherCodes.h"

#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QUrlQuery>

namespace {

constexpr auto kBase = "https://restapi.amap.com/v3";

} // namespace

AmapProvider::AmapProvider(ConfigStore *configs, QNetworkAccessManager *nam, QObject *parent)
    : WeatherProvider(nam, parent)
    , m_configs(configs)
{
}

QString AmapProvider::key() const
{
    if (m_configs) {
        if (const auto v = m_configs->value(QStringLiteral("weather.keys.amap.key")))
            return v->toString().trimmed();
    }
    return {};
}

bool AmapProvider::isConfigured() const
{
    return !key().isEmpty();
}

QString AmapProvider::errorKindFromBody(const QJsonObject &body) const
{
    if (body.value(QLatin1String("status")).toString() == QLatin1String("1"))
        return {};
    const QString info = body.value(QLatin1String("info")).toString() + QLatin1Char(' ')
                         + body.value(QLatin1String("infocode")).toString();
    if (info.contains(QLatin1String("LIMIT"), Qt::CaseInsensitive)
        || info.contains(QLatin1String("EXCEED"), Qt::CaseInsensitive)
        || info.contains(QLatin1String("OVERDRIVE"), Qt::CaseInsensitive)
        || info.contains(QLatin1String("QUOTA"), Qt::CaseInsensitive))
        return QStringLiteral("quota");
    return QStringLiteral("auth");
}

void AmapProvider::searchCity(const QString &keyword)
{
    const QString trimmed = keyword.trimmed();
    if (trimmed.isEmpty()) {
        emit citySearchFinished({});
        return;
    }

    // geocode/geo（batch=true 返回多个候选，覆盖区县级匹配）；location 为 "lon,lat"
    QUrl url(QLatin1String(kBase) + QStringLiteral("/geocode/geo"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("key"), key());
    query.addQueryItem(QStringLiteral("address"), trimmed);
    query.addQueryItem(QStringLiteral("batch"), QStringLiteral("true"));
    query.addQueryItem(QStringLiteral("output"), QStringLiteral("JSON"));
    url.setQuery(query);

    getJson(url, {}, cwn::weather::kSearchTimeoutMs,
            [this](const QJsonDocument &doc, const QString &errorKind, int) {
                QVariantList cities;
                if (errorKind.isEmpty()
                    && doc.object().value(QLatin1String("status")).toString()
                        == QLatin1String("1")) {
                    for (const QJsonValue &item :
                         doc.object().value(QLatin1String("geocodes")).toArray()) {
                        const QJsonObject obj = item.toObject();
                        const QString adcode = obj.value(QLatin1String("adcode")).toString();
                        const QString lonLat = obj.value(QLatin1String("location")).toString();
                        const int comma = lonLat.indexOf(QLatin1Char(','));
                        if (adcode.isEmpty() || comma <= 0)
                            continue;
                        bool latOk = false;
                        bool lonOk = false;
                        const double lon = lonLat.left(comma).toDouble(&lonOk);
                        const double lat = lonLat.mid(comma + 1).toDouble(&latOk);
                        if (!latOk || !lonOk)
                            continue;
                        QString province = obj.value(QLatin1String("province")).toString();
                        QString cityName = obj.value(QLatin1String("city")).toString();
                        const QString district =
                            obj.value(QLatin1String("district")).toString();
                        // 直辖市 city 字段是 "[]" 字面量
                        if (cityName == QLatin1String("[]"))
                            cityName.clear();
                        const QString name = !district.isEmpty()
                                                 ? district
                                                 : (!cityName.isEmpty() ? cityName : province);

                        QVariantMap city;
                        city.insert(QStringLiteral("cityId"), adcode);
                        city.insert(QStringLiteral("name"), name);
                        city.insert(QStringLiteral("province"), province);
                        city.insert(QStringLiteral("lat"), lat);
                        city.insert(QStringLiteral("lon"), lon);
                        city.insert(QStringLiteral("adcode"), adcode);
                        cities.append(city);
                    }
                }
                emit citySearchFinished(cities);
            });
}

void AmapProvider::fetch(const CityInfo &city)
{
    const QString cached =
        city.adcode.isEmpty() ? m_adcodeCache.value(city.cityId) : city.adcode;
    if (!cached.isEmpty()) {
        fetchWeather(city, cached);
        return;
    }
    resolveAdcodeAndFetch(city);
}

void AmapProvider::resolveAdcodeAndFetch(const CityInfo &city)
{
    // 城市来自其他源的搜索时缺 adcode：geocode/regeo 由经纬度解析（同 key）
    QUrl url(QLatin1String(kBase) + QStringLiteral("/geocode/regeo"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("key"), key());
    query.addQueryItem(
        QStringLiteral("location"),
        QStringLiteral("%1,%2").arg(QString::number(city.longitude, 'g', 12),
                                    QString::number(city.latitude, 'g', 12)));
    url.setQuery(query);

    getJson(url, {}, cwn::weather::kSearchTimeoutMs,
            [this, city](const QJsonDocument &doc, const QString &errorKind, int) {
                QString kind = errorKind;
                QString adcode;
                if (kind.isEmpty()) {
                    kind = errorKindFromBody(doc.object());
                    if (kind.isEmpty()) {
                        adcode = doc.object()
                                     .value(QLatin1String("regeocode"))
                                     .toObject()
                                     .value(QLatin1String("addressComponent"))
                                     .toObject()
                                     .value(QLatin1String("adcode"))
                                     .toString();
                        if (adcode.isEmpty())
                            kind = QStringLiteral("parse");
                    }
                }
                if (!kind.isEmpty()) {
                    emit fetchFinished(city.cityId, {}, kind);
                    return;
                }
                m_adcodeCache.insert(city.cityId, adcode);
                fetchWeather(city, adcode);
            });
}

void AmapProvider::fetchWeather(const CityInfo &city, const QString &adcode)
{
    if (m_pending.contains(city.cityId))
        return;
    m_pending.insert(city.cityId, Pending{ {}, {}, 2 });

    auto request = [this, city, adcode](bool base) {
        QUrl url(QLatin1String(kBase) + QStringLiteral("/weather/weatherInfo"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("key"), key());
        query.addQueryItem(QStringLiteral("city"), adcode);
        query.addQueryItem(QStringLiteral("extensions"), base ? QStringLiteral("base")
                                                              : QStringLiteral("all"));
        query.addQueryItem(QStringLiteral("output"), QStringLiteral("JSON"));
        url.setQuery(query);

        getJson(url, {}, cwn::weather::kFetchTimeoutMs,
                [this, cityId = city.cityId, base](const QJsonDocument &doc,
                                                   const QString &errorKind, int) {
                    auto it = m_pending.find(cityId);
                    if (it == m_pending.end())
                        return; // 另一请求已终结本轮
                    QString kind = errorKind;
                    if (kind.isEmpty())
                        kind = errorKindFromBody(doc.object());
                    if (!kind.isEmpty()) {
                        m_pending.erase(it);
                        emit fetchFinished(cityId, {}, kind);
                        return;
                    }

                    const QJsonObject body = doc.object();
                    if (base) {
                        const QJsonArray lives = body.value(QLatin1String("lives")).toArray();
                        if (!lives.isEmpty()) {
                            const QJsonObject life = lives.at(0).toObject();
                            QVariantMap current;
                            bool tempOk = false;
                            bool humidityOk = false;
                            const double temperature =
                                life.value(QLatin1String("temperature")).toString().toDouble(&tempOk);
                            const double humidity =
                                life.value(QLatin1String("humidity")).toString().toDouble(&humidityOk);
                            if (tempOk)
                                current.insert(QStringLiteral("temperature"), temperature);
                            if (humidityOk)
                                current.insert(QStringLiteral("humidity"), humidity);
                            current.insert(
                                QStringLiteral("weatherCode"),
                                cwn::weather::codeFromAmapText(
                                    life.value(QLatin1String("weather")).toString()));
                            // 高德只有风力等级文本（"≤3"/"4" 等），不换算风速，
                            // 另存 windScale 展示；windSpeed 留空
                            current.insert(QStringLiteral("windScale"),
                                           life.value(QLatin1String("windpower")).toString());
                            it->current = current;
                        }
                    } else {
                        const QJsonArray forecasts =
                            body.value(QLatin1String("forecasts")).toArray();
                        if (!forecasts.isEmpty()) {
                            const QJsonArray casts =
                                forecasts.at(0).toObject().value(QLatin1String("casts")).toArray();
                            if (!casts.isEmpty()) {
                                const QJsonObject cast = casts.at(0).toObject();
                                bool maxOk = false;
                                bool minOk = false;
                                const double tempMax =
                                    cast.value(QLatin1String("daytemp")).toString().toDouble(&maxOk);
                                const double tempMin = cast.value(QLatin1String("nighttemp"))
                                                           .toString()
                                                           .toDouble(&minOk);
                                QVariantMap today;
                                if (maxOk)
                                    today.insert(QStringLiteral("tempMax"), tempMax);
                                if (minOk)
                                    today.insert(QStringLiteral("tempMin"), tempMin);
                                today.insert(
                                    QStringLiteral("dayCode"),
                                    cwn::weather::codeFromAmapText(
                                        cast.value(QLatin1String("dayweather")).toString()));
                                today.insert(
                                    QStringLiteral("nightCode"),
                                    cwn::weather::codeFromAmapText(
                                        cast.value(QLatin1String("nightweather")).toString()));
                                it->today = today;
                            }
                        }
                    }

                    if (--it->remaining <= 0) {
                        WeatherProvider::Snapshot snapshot;
                        snapshot.valid = !it->current.isEmpty() || !it->today.isEmpty();
                        snapshot.current = it->current;
                        snapshot.today = it->today;
                        m_pending.erase(it);
                        emit fetchFinished(cityId, snapshot, QString());
                    }
                });
    };
    request(true);
    request(false);
}

void AmapProvider::testConnection()
{
    // 用北京 adcode 作探针（status/infocode 判别 key 有效性与配额）
    QUrl url(QLatin1String(kBase) + QStringLiteral("/weather/weatherInfo"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("key"), key());
    query.addQueryItem(QStringLiteral("city"), QStringLiteral("110000"));
    query.addQueryItem(QStringLiteral("extensions"), QStringLiteral("base"));
    query.addQueryItem(QStringLiteral("output"), QStringLiteral("JSON"));
    url.setQuery(query);

    getJson(url, {}, cwn::weather::kFetchTimeoutMs,
            [this](const QJsonDocument &doc, const QString &errorKind, int) {
                if (!errorKind.isEmpty()) {
                    emit connectionTestFinished(false, errorKind);
                    return;
                }
                const QString bodyKind = errorKindFromBody(doc.object());
                emit connectionTestFinished(bodyKind.isEmpty(), bodyKind);
            });
}
