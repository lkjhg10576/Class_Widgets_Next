#pragma once

#include "../WeatherProvider.h"

class ConfigStore;

// 和风天气数据源（v1 新接口）。每项目分配专属 API Host（xxx.qweatherapi.com），
// 鉴权用 API Key（X-QW-Api-Key 头；JWT/Ed25519 为后续增强）。
// fetch 内部聚合 current + daily + weatheralert 三请求后一次 fetchFinished；
// 预警请求失败可降级（实况/预报照常展示）。湿度和降水概率为 0-1 比率，归一
// 时 ×100；归属声明要求随数据展示，设置页"数据源"卡静态承载。
class QweatherProvider final : public WeatherProvider
{
    Q_OBJECT
public:
    QweatherProvider(ConfigStore *configs, QNetworkAccessManager *nam, QObject *parent = nullptr);

    QString id() const override { return QStringLiteral("qweather"); }
    bool isConfigured() const override;
    void searchCity(const QString &keyword) override;
    void fetch(const CityInfo &city) override;
    void testConnection() override;

private:
    QString key() const;
    QString host() const; // 去 scheme/尾斜杠后的专属 Host
    QUrl apiUrl(const QString &path, const QUrlQuery &query) const;
    std::list<std::pair<QString, QString>> authHeader() const;
    // v1 响应成功无 code 字段（视为成功）；错误体 code：401/403→auth、402/429→quota
    static QString errorKindForCode(const QString &code);

    void handleFetchResponse(const QString &cityId, int which, const QJsonDocument &doc,
                             const QString &transportKind);

    ConfigStore *m_configs = nullptr;
    // which：0=current 1=daily 2=alert（alert 可降级，其余失败终结本轮）
    struct Pending
    {
        QVariantMap current;
        QVariantMap today;
        QVariantList alerts;
        int remaining = 0;
    };
    QHash<QString, Pending> m_pending; // cityId → 三请求聚合状态
};
