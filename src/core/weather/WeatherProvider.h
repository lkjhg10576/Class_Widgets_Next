#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <list>
#include <utility>

class QNetworkAccessManager;

namespace cwn::weather {
// 拉取/搜索超时（原 WeatherService.cpp 常量的公共化，各 Provider 共用）
inline constexpr int kFetchTimeoutMs = 10 * 1000;
inline constexpr int kSearchTimeoutMs = 5 * 1000;

// QJsonValue 的数值读取（toDouble 不带 bool* 出参）：非数值 → false
inline bool readDouble(const QJsonValue &value, double &out)
{
    if (!value.isDouble())
        return false;
    out = value.toDouble();
    return true;
}
} // namespace cwn::weather

// 天气数据源抽象：一个实现 = 一个上游（小米/高德/和风/华风爱科/彩云）。
// 实现负责鉴权、请求编排（多请求源在内部聚合为一次结果）与字段归一；
// WeatherService 作门面持有多实现，负责轮询/缓存/过期/去重/预警推送。
//
// 归一快照（即 QML weatherData() 的数值语义，完整契约见 WeatherService.h）：
//   current: { temperature, feelsLike?, humidity(0-100), windSpeed(m/s)?,
//              windScale?(风力等级文本，高德专用), weatherCode }
//   today:   { tempMax, tempMin, dayCode, nightCode, precipProb(0-100)?,
//              sunrise?("HH:MM")?, sunset? }
//   alerts:  [ { alertId, type, level("B"|"Y"|"O"|"R"), levelRaw, title, pubTime } ]
//   （pm25/aqi 仅小米源提供；AQI 增强不在本期范围）
//
// errorKind 约定（随 fetchFinished/connectionTestFinished 上抛，门面据此冷却退避）：
//   ""（成功）|"auth"（密钥无效/未授权）|"quota"（配额超限/欠费）|"network"|"parse"
class WeatherProvider : public QObject
{
    Q_OBJECT
public:
    // widget settings.city 的原值键为 {"cityId","name","lat","lon","province"}；
    // 数据源扩展键只增不改（向后兼容旧数据）：adcode（高德行政区划码）、
    // wcnKey（华风爱科 Location Key）。经纬度为各源通用锚点。
    struct CityInfo
    {
        QString cityId;
        QString name;
        QString province;
        double latitude = 0;
        double longitude = 0;
        QString adcode;
        QString wcnKey;
        bool isValid() const { return !cityId.isEmpty() && !name.isEmpty(); }
    };

    struct Snapshot
    {
        bool valid = false;
        QVariantMap current;
        QVariantMap today;
        QVariantList alerts;
    };

    // 公共 GET-JSON 回调：doc 为成功解析的响应（对象或数组）；
    // errorKind 非空时 doc 为空文档。HTTP 状态原样附带供实现解释 body 语义。
    using JsonCallback =
        std::function<void(const QJsonDocument &doc, const QString &errorKind, int httpStatus)>;

    explicit WeatherProvider(QNetworkAccessManager *nam, QObject *parent = nullptr);

    virtual QString id() const = 0;
    // 凭据是否齐备（只查形态不查有效性；有效性由 fetch/testConnection 的 errorKind 反馈）
    virtual bool isConfigured() const = 0;
    virtual void searchCity(const QString &keyword) = 0; // 完成 → citySearchFinished
    virtual void fetch(const CityInfo &city) = 0;        // 完成 → fetchFinished
    virtual void testConnection() = 0;                   // 完成 → connectionTestFinished
    // 无搜索能力的源（彩云）返回 false，门面把城市搜索路由到小米
    virtual bool supportsCitySearch() const { return true; }

signals:
    void citySearchFinished(const QVariantList &cities);
    void fetchFinished(const QString &cityId, const WeatherProvider::Snapshot &snapshot,
                       const QString &errorKind);
    void connectionTestFinished(bool ok, const QString &errorKind);

protected:
    // 公共 GET-JSON：传输错误/HTTP 错误 → network/auth/quota（按状态码映射），
    // 非 JSON 响应 → parse；HTTP 200 时的应用层错误（如高德 status"0"、
    // 彩云 status"failed"）由实现解释 body 后自行归类
    void getJson(const QUrl &url, const std::list<std::pair<QString, QString>> &headers,
                 int timeoutMs, const JsonCallback &callback);
    static QString errorKindForHttp(int httpStatus);

    QNetworkAccessManager *m_nam = nullptr;
};
