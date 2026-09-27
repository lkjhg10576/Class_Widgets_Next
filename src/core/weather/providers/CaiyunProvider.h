#pragma once

#include "../WeatherProvider.h"

class ConfigStore;

// 彩云天气数据源（https://api.caiyunapp.com/v2.6，收费，按量/包月计费）。
// 综合接口一次拉全（realtime + daily + alert），token 嵌 URL 路径；
// URL 路径经度在前（响应 location 字段却是 [纬度,经度]，勿混淆）。
// 彩云无城市搜索接口 → supportsCitySearch()=false，门面把搜索路由到小米源
// （结果仅经纬度被本源消费）。alert 块需 token 具备预警权限，缺失时静默留空。
// skycon 为字符串枚举（CLEAR_DAY 等 20 种），经 WeatherCodes 映射为规范码。
class CaiyunProvider final : public WeatherProvider
{
    Q_OBJECT
public:
    CaiyunProvider(ConfigStore *configs, QNetworkAccessManager *nam, QObject *parent = nullptr);

    QString id() const override { return QStringLiteral("caiyun"); }
    bool isConfigured() const override;
    void searchCity(const QString &keyword) override; // 不可达：门面路由到小米，兜底发空
    void fetch(const CityInfo &city) override;
    void testConnection() override;

private:
    QString token() const;
    void fetchUrl(const QUrl &url, const QString &cityId, bool test);
    // {"status":"failed","error":"token is invalid"} → invalid/token → auth，其余 → quota
    QString errorKindFromBody(const QJsonObject &body) const;

    ConfigStore *m_configs = nullptr;
};
