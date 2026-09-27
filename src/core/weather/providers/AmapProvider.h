#pragma once

#include "../WeatherProvider.h"

class ConfigStore;

// 高德天气数据源（https://restapi.amap.com/v3/weather/weatherInfo）。
// 实况（extensions=base）与预报（extensions=all）是两个接口，fetch 内部聚合
// 两请求后一次 fetchFinished，一轮刷新计 2 次天气配额（免费档 5,000 次/月）。
// 高德无预警/无 AQI/无日出日落/无降水概率，快照相应字段留空（QML 已容错）；
// weather 只返回中文文本，经 WeatherCodes::codeFromAmapText 映射为规范码。
// 城市搜索/坐标→adcode 用 geocode API（同 key，计入基础 LBS 配额）：
// 已知 adcode 时直接取天气；城市来自其他源时用 regeo 由经纬度解析（会话内缓存）。
class AmapProvider final : public WeatherProvider
{
    Q_OBJECT
public:
    AmapProvider(ConfigStore *configs, QNetworkAccessManager *nam, QObject *parent = nullptr);

    QString id() const override { return QStringLiteral("amap"); }
    bool isConfigured() const override;
    void searchCity(const QString &keyword) override;
    void fetch(const CityInfo &city) override;
    void testConnection() override;

private:
    QString key() const;
    void resolveAdcodeAndFetch(const CityInfo &city);
    void fetchWeather(const CityInfo &city, const QString &adcode);
    // 高德恒返回 HTTP 200，错误在 body：status "1"=成功；info/infocode 含
    // LIMIT/EXCEED 等关键字 → quota，其余（key 无效/未开通）→ auth
    QString errorKindFromBody(const QJsonObject &body) const;

    ConfigStore *m_configs = nullptr;
    QHash<QString, QString> m_adcodeCache; // cityId → adcode（会话内缓存）
    struct Pending
    {
        QVariantMap current;
        QVariantMap today;
        int remaining = 0;
    };
    QHash<QString, Pending> m_pending; // cityId → base+all 双请求聚合状态
};
