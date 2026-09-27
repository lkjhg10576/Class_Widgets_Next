#pragma once

#include "../WeatherProvider.h"

class ConfigStore;

// 华风爱科数据源（https://openapi.weathercn.com，AccuWeather 风格 PascalCase
// 响应 + Metric/Imperial 双单位）。鉴权用 API Key（X-Gw-API-Key 头，避免进
// URL 日志）。fetch 内部聚合 currentconditions + 5day forecasts + alerts 三
// 请求后一次 fetchFinished；预警请求失败可降级。
// 城市定位：搜索走 /locations/v1/cities/translate；城市来自其他源时用
// geoposition 由经纬度惰性解析 Location Key（会话内缓存）；温度取 Metric，
// 风速 km/h ÷ 3.6 归一 m/s；现象码由 WeatherIcon 1-47 映射。
class WeathercnProvider final : public WeatherProvider
{
    Q_OBJECT
public:
    WeathercnProvider(ConfigStore *configs, QNetworkAccessManager *nam, QObject *parent = nullptr);

    QString id() const override { return QStringLiteral("weathercn"); }
    bool isConfigured() const override;
    void searchCity(const QString &keyword) override;
    void fetch(const CityInfo &city) override;
    void testConnection() override;

private:
    QString key() const;
    QUrl apiUrl(const QString &path, const QUrlQuery &query) const;
    std::list<std::pair<QString, QString>> authHeader() const;
    void resolveKeyAndFetch(const CityInfo &city);
    void fetchWeather(const CityInfo &city, const QString &locationKey);
    void handleFetchResponse(const QString &cityId, int which, const QJsonDocument &doc,
                             const QString &transportKind);

    ConfigStore *m_configs = nullptr;
    QHash<QString, QString> m_keyCache; // cityId → 华风 Location Key（会话内缓存）
    // which：0=current 1=daily 2=alert（alert 可降级，其余失败终结本轮）
    struct Pending
    {
        QVariantMap current;
        QVariantMap today;
        QVariantList alerts;
        int remaining = 0;
    };
    QHash<QString, Pending> m_pending;
};
