#include "WeatherService.h"

#include "../ConfigStore.h"
#include "../Logger.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

namespace Log = cwn::Log; // 命名空间别名：MSVC 拒绝 using cwn::Log;（C2873）

namespace {

// 数据源常量（weather.rs:26-28 原文；无动态签名，固定常量）
constexpr auto kBaseUrl = "https://weatherapi.market.xiaomi.com/wtr-v3";
constexpr auto kAppKey = "weather20151024";
constexpr auto kSign = "zUFJoAR2ZVrDy1vF3D07";

// 轮询参数（weather.rs:18-24）
constexpr int kDefaultPollIntervalSecs = 3600;
constexpr int kMinPollIntervalSecs = 1800;
constexpr int kMaxPollIntervalSecs = 10800;
constexpr int kPollTickMs = 60 * 1000; // IDLE_WAKE_SECS：60s 唤醒检查过期
constexpr int kFetchTimeoutMs = 10 * 1000;
constexpr int kSearchTimeoutMs = 5 * 1000;

qint64 unixNow()
{
    return QDateTime::currentSecsSinceEpoch();
}

// 夜间判定：19:00–06:00 按本机时钟（上游速报窗口用固定东八区，此处仅影响图标昼夜档）
bool isNightNow()
{
    const int hour = QDateTime::currentDateTime().time().hour();
    return hour >= 19 || hour < 6;
}

// 天气现象代码 → Fluent 图标（粒度对齐上游 utils/weather.ts weatherCodeToIcon，
// 落到 RinUI 自带的 ic_fluent_weather_* 字形；0/1/3/13/26 分昼夜档，
// 20 沙尘暴用 duststorm——上游兜底到 sun 的粗糙点在此修掉）
QString weatherCodeToIcon(int code, bool isNight)
{
    if (code == 0)
        return isNight ? QStringLiteral("ic_fluent_weather_moon_20_regular")
                       : QStringLiteral("ic_fluent_weather_sunny_20_regular");
    if (code == 1)
        return isNight ? QStringLiteral("ic_fluent_weather_partly_cloudy_night_20_regular")
                       : QStringLiteral("ic_fluent_weather_partly_cloudy_day_20_regular");
    if (code == 2)
        return QStringLiteral("ic_fluent_weather_cloudy_20_regular");
    if (code == 3)
        return isNight ? QStringLiteral("ic_fluent_weather_rain_showers_night_20_regular")
                       : QStringLiteral("ic_fluent_weather_rain_showers_day_20_regular");
    if (code == 4 || code == 5)
        return QStringLiteral("ic_fluent_weather_thunderstorm_20_regular");
    if (code == 6 || code == 19)
        return QStringLiteral("ic_fluent_weather_rain_snow_20_regular");
    if (code == 7 || code == 21 || code == 301)
        return QStringLiteral("ic_fluent_weather_drizzle_20_regular");
    if (code == 8 || code == 22 || (code >= 9 && code <= 12) || (code >= 23 && code <= 25))
        return QStringLiteral("ic_fluent_weather_rain_20_regular");
    if (code == 13 || code == 26)
        return isNight ? QStringLiteral("ic_fluent_weather_snow_shower_night_20_regular")
                       : QStringLiteral("ic_fluent_weather_snow_shower_day_20_regular");
    if ((code >= 14 && code <= 17) || (code >= 27 && code <= 28) || code == 302)
        return QStringLiteral("ic_fluent_weather_snow_20_regular");
    if (code == 18 || code == 32 || code == 35 || code == 49 || code == 57 || code == 58)
        return QStringLiteral("ic_fluent_weather_fog_20_regular");
    if (code >= 53 && code <= 56)
        return QStringLiteral("ic_fluent_weather_haze_20_regular");
    if (code == 20)
        return QStringLiteral("ic_fluent_weather_duststorm_20_regular");
    return QStringLiteral("ic_fluent_weather_sunny_20_regular");
}

// 可缺省数值字段：字符串不可解析时返回 invalid QVariant（上游 Option::None 语义）
QVariant optionalDouble(const QJsonValue &value)
{
    bool ok = false;
    const double parsed = value.toString().toDouble(&ok);
    return ok ? QVariant(parsed) : QVariant();
}

} // namespace

WeatherService::WeatherService(ConfigStore *configs, QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_configs(configs)
{
    // 60s 唤醒 + "间隔的 90%"过期判定：上游主循环的事件驱动等价移植
    m_pollTimer.setInterval(kPollTickMs);
    connect(&m_pollTimer, &QTimer::timeout, this, &WeatherService::onPollTick);
}

QString WeatherService::widgetTypeId()
{
    return QStringLiteral("classwidgets.weather");
}

WeatherService::~WeatherService() = default;

void WeatherService::start()
{
    if (!m_pollTimer.isActive())
        m_pollTimer.start();
}

void WeatherService::onPollTick()
{
    // 每次唤醒重读间隔配置（上游已知缺口"设置落盘但不回读"在此修掉：改动立即生效）
    if (m_configs) {
        if (const auto v = m_configs->value(QStringLiteral("weather.poll_interval"))) {
            m_pollIntervalSecs =
                qBound(kMinPollIntervalSecs, v->toInt(kDefaultPollIntervalSecs),
                       kMaxPollIntervalSecs);
        }
    }

    const qint64 dueThreshold = qint64(m_pollIntervalSecs) * 9 / 10;
    const qint64 now = unixNow();
    for (auto it = m_activeCities.constBegin(); it != m_activeCities.constEnd(); ++it) {
        const Snapshot snapshot = m_cache.value(it.key());
        if (snapshot.lastFetchAt == 0 || now - snapshot.lastFetchAt >= dueThreshold)
            fetchNow(it.value());
    }
}

void WeatherService::searchCity(const QString &keyword)
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

    QNetworkRequest request(url);
    request.setTransferTimeout(kSearchTimeoutMs);
    QNetworkReply *reply = m_nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        QVariantList cities;
        if (reply->error() != QNetworkReply::NoError) {
            Log::warn(
                QStringLiteral("Weather city search failed: %1").arg(reply->errorString()));
            emit citySearchFinished(cities);
            return;
        }

        // 响应顶层是 JSON 数组；latitude/longitude 为字符串编码
        const QJsonArray items = QJsonDocument::fromJson(reply->readAll()).array();
        for (const QJsonValue &item : items) {
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
        emit citySearchFinished(cities);
    });
}

void WeatherService::request(const QString &cityJson)
{
    const CityInfo city = cityFromJson(cityJson);
    if (!city.isValid())
        return; // 未配置城市：组件展示占位（上游"无城市=功能静默不拉取"语义）

    m_activeCities.insert(city.cityId, city);
    const Snapshot snapshot = m_cache.value(city.cityId);
    if (!snapshot.valid || !isFresh(snapshot))
        fetchNow(city);
}

void WeatherService::refresh(const QString &cityJson)
{
    const CityInfo city = cityFromJson(cityJson);
    if (!city.isValid())
        return;
    m_activeCities.insert(city.cityId, city);
    fetchNow(city);
}

QVariantMap WeatherService::weatherData(const QString &cityJson) const
{
    QVariantMap result;
    result.insert(QStringLiteral("status"), QStringLiteral("empty"));

    const CityInfo city = cityFromJson(cityJson);
    if (!city.isValid())
        return result;
    result.insert(QStringLiteral("cityName"), city.name);

    const Snapshot snapshot = m_cache.value(city.cityId);
    if (!snapshot.valid)
        return result;

    result.insert(QStringLiteral("status"),
                  snapshot.failed ? QStringLiteral("stale") : QStringLiteral("ok"));
    result.insert(QStringLiteral("lastFetchAt"), snapshot.lastFetchAt);

    QVariantMap current = snapshot.current;
    // 图标读取时按昼夜动态解析（0=晴、1=多云 等分日月档）
    if (current.contains(QStringLiteral("weatherCode"))) {
        current.insert(QStringLiteral("icon"),
                       weatherCodeToIcon(
                           current.value(QStringLiteral("weatherCode")).toInt(),
                           isNightNow()));
    }
    result.insert(QStringLiteral("current"), current);
    result.insert(QStringLiteral("today"), snapshot.today);
    result.insert(QStringLiteral("alerts"), snapshot.alerts);
    return result;
}

WeatherService::CityInfo WeatherService::cityFromJson(const QString &cityJson)
{
    const QJsonDocument doc = QJsonDocument::fromJson(cityJson.toUtf8());
    if (!doc.isObject())
        return {};
    const QJsonObject obj = doc.object();
    CityInfo city;
    city.cityId = obj.value(QLatin1String("cityId")).toString();
    city.name = obj.value(QLatin1String("name")).toString();
    city.province = obj.value(QLatin1String("province")).toString();
    city.latitude = obj.value(QLatin1String("lat")).toDouble();
    city.longitude = obj.value(QLatin1String("lon")).toDouble();
    if (!city.isValid())
        return {};
    return city;
}

void WeatherService::fetchNow(const CityInfo &city)
{
    if (m_inflight.contains(city.cityId))
        return; // 同城市去重，等在途回包

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

    QNetworkRequest request(url);
    request.setTransferTimeout(kFetchTimeoutMs);
    QNetworkReply *reply = m_nam->get(request);
    m_inflight.insert(city.cityId, reply);
    emit busyChanged();

    connect(reply, &QNetworkReply::finished, this, [this, reply, city] {
        m_inflight.remove(city.cityId);
        emit busyChanged();
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            Log::warn(QStringLiteral("Weather fetch failed for %1: %2")
                          .arg(city.name, reply->errorString()));
            markFailed(city.cityId);
            return;
        }

        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            Log::warn(
                QStringLiteral("Weather response is not valid JSON for %1").arg(city.name));
            markFailed(city.cityId);
            return;
        }
        applySnapshot(city, doc.object());
    });
}

void WeatherService::applySnapshot(const CityInfo &city, const QJsonObject &body)
{
    // 小米接口数值字段全部是字符串编码（"value": "22"），一律 string → number 解析；
    // 必填字段缺失/不可解析时整块丢弃（上游 None 静默降级语义）
    const QJsonObject currentObj = body.value(QLatin1String("current")).toObject();
    bool tempOk = false;
    bool codeOk = false;
    bool feelsOk = false;
    bool humidityOk = false;
    bool windOk = false;
    const double temperature = currentObj.value(QLatin1String("temperature"))
                                   .toObject()
                                   .value(QLatin1String("value"))
                                   .toString()
                                   .toDouble(&tempOk);
    const int weatherCode =
        currentObj.value(QLatin1String("weather")).toString().toInt(&codeOk);
    const double feelsLike = currentObj.value(QLatin1String("feelsLike"))
                                 .toObject()
                                 .value(QLatin1String("value"))
                                 .toString()
                                 .toDouble(&feelsOk);
    const double humidity = currentObj.value(QLatin1String("humidity"))
                                .toObject()
                                .value(QLatin1String("value"))
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
        const QVariant pm25 = optionalDouble(aqiObj.value(QLatin1String("pm25")));
        const QVariant aqi = optionalDouble(aqiObj.value(QLatin1String("aqi")));
        if (!pm25.isNull())
            current.insert(QStringLiteral("pm25"), pm25);
        if (!aqi.isNull())
            current.insert(QStringLiteral("aqi"), aqi);
    }

    // 逐日预报是"列式"数组：每个属性的 value 按天排列，索引 0 = 今天（上游只
    // 消费前两天，本移植仅用今天）
    QVariantMap today;
    const QJsonObject daily = body.value(QLatin1String("forecastDaily")).toObject();
    const QJsonArray tempVals = daily.value(QLatin1String("temperature"))
                                    .toObject()
                                    .value(QLatin1String("value"))
                                    .toArray();
    const QJsonArray weatherVals = daily.value(QLatin1String("weather"))
                                       .toObject()
                                       .value(QLatin1String("value"))
                                       .toArray();
    const QJsonArray precipVals = daily.value(QLatin1String("precipitationProbability"))
                                      .toObject()
                                      .value(QLatin1String("value"))
                                      .toArray();
    const QJsonArray sunVals = daily.value(QLatin1String("sunRiseSet"))
                                   .toObject()
                                   .value(QLatin1String("value"))
                                   .toArray();
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
        const int dayCode = weatherVals.at(0)
                                .toObject()
                                .value(QLatin1String("from"))
                                .toString()
                                .toInt(&dayOk);
        const int nightCode = weatherVals.at(0)
                                  .toObject()
                                  .value(QLatin1String("to"))
                                  .toString()
                                  .toInt(&nightOk);
        const double precipProb = precipVals.at(0).toString().toDouble(&precipOk);
        if (maxOk && minOk && dayOk && nightOk && precipOk) {
            today.insert(QStringLiteral("tempMax"), tempMax);
            today.insert(QStringLiteral("tempMin"), tempMin);
            today.insert(QStringLiteral("dayCode"), dayCode);
            today.insert(QStringLiteral("nightCode"), nightCode);
            today.insert(QStringLiteral("precipProb"), precipProb);
            // 日出/日落：接口返回完整 ISO 时间（"2026-09-15T05:55:00+08:00"），
            // 取第 11-15 字符得 "HH:MM"（原 left(5) 会得到 "2026-"；格式不符时
            // mid 返回空串，由下方 isEmpty 判断丢弃该字段）
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

    // 预警：level 原样透传（B/Y/O/R 或"蓝色"等中文），文案与配色归一在 QML 侧
    QVariantList alerts;
    const QJsonArray alertArr = body.value(QLatin1String("alerts")).toArray();
    for (const QJsonValue &item : alertArr) {
        const QJsonObject obj = item.toObject();
        const QString alertId = obj.value(QLatin1String("alertId")).toString();
        const QString typeName = obj.value(QLatin1String("type")).toString();
        if (alertId.isEmpty() || typeName.isEmpty())
            continue;
        QVariantMap alert;
        alert.insert(QStringLiteral("alertId"), alertId);
        alert.insert(QStringLiteral("type"), typeName);
        alert.insert(QStringLiteral("level"), obj.value(QLatin1String("level")).toString());
        alert.insert(QStringLiteral("title"), obj.value(QLatin1String("title")).toString());
        alert.insert(QStringLiteral("pubTime"),
                     obj.value(QLatin1String("pubTime")).toString());
        alerts.append(alert);
    }

    Snapshot snapshot;
    snapshot.valid = true;
    snapshot.failed = false;
    snapshot.lastFetchAt = unixNow();
    snapshot.current = current;
    snapshot.today = today;
    snapshot.alerts = alerts;
    m_cache.insert(city.cityId, snapshot);

    Log::info(QStringLiteral("Weather updated for %1 (current=%2, alerts=%3)")
                  .arg(city.name)
                  .arg(current.isEmpty() ? QStringLiteral("none") : QStringLiteral("ok"))
                  .arg(alerts.size()));
    emit weatherUpdated(city.cityId);
}

bool WeatherService::isFresh(const Snapshot &snapshot) const
{
    // 拉取触发阈值 = 间隔的 90%（weather.rs due_threshold 语义）
    const qint64 dueThreshold = qint64(m_pollIntervalSecs) * 9 / 10;
    return snapshot.lastFetchAt > 0 && unixNow() - snapshot.lastFetchAt < dueThreshold;
}

void WeatherService::markFailed(const QString &cityId)
{
    // 上游语义：失败只标记 fetchStatus=failed，旧快照保留降级展示，不动 lastFetchAt
    m_cache[cityId].failed = true;
    emit weatherUpdated(cityId);
}
