#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

class ConfigStore;
class QNetworkAccessManager;
class QNetworkReply;

// 天气小组件数据源：小米天气 wtr-v3（移植自 NetSpeed-Dynamic src-tauri/src/weather.rs）。
// 数据源无需申请 key：appKey/sign 为该公开接口的固定常量（上游模块头原文声明）。
//
// 职责：城市搜索（location/city/search）+ 实况/今日预报/预警合并拉取（weather/all）
// + 按"间隔的 90%"过期重拉。天气现象代码 → 文本/预警等级 → 文案 的展示映射放在
// QML 侧（widgets/weather.qml，对齐上游 utils/weather.ts），C++ 只负责取数与数值契约。
//
// 线程模型：上游为专用阻塞线程 + 60s 唤醒轮询；Qt 侧 QNetworkAccessManager 本身异步，
// 以 QTimer 60s tick 事件驱动等价实现，全部回调在 GUI 线程。
class WeatherService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit WeatherService(ConfigStore *configs, QObject *parent = nullptr);
    // QHash 值类型 QNetworkReply 仅前置声明：析构须在 cpp（完整类型处）定义
    ~WeatherService() override;

    // BuiltinWidgets 注册表中的 widget_id；AppCentral 以此为天气组件挂专属 backend
    static QString widgetTypeId();

    // AppCentral 在 configs->load() 之后调用：启动 60s 轮询 tick
    void start();

    bool busy() const { return !m_inflight.isEmpty(); }

    // --- QML 契约 ---
    // cityJson 即 widget 实例 settings.city 的原值：
    // {"cityId":"101010100","name":"北京","lat":39.9,"lon":116.4,"province":"北京市"}
    // （与上游 nsd_weather_city 的 serde camelCase 契约一致）
    Q_INVOKABLE void searchCity(const QString &keyword); // 异步 → citySearchFinished
    Q_INVOKABLE void request(const QString &cityJson);   // 缺数据或已过期才拉取
    Q_INVOKABLE void refresh(const QString &cityJson);   // 忽略过期时间强制拉取
    // 返回缓存快照（status 语义对齐上游 fetchStatus + 未配置/未就绪态）：
    // { status: "empty"|"stale"|"ok", cityName, lastFetchAt,
    //   current: { temperature, weatherCode, feelsLike, humidity, windSpeed,
    //              pm25?, aqi?, icon },
    //   today: { tempMax, tempMin, dayCode, nightCode, precipProb, sunrise?, sunset? },
    //   alerts: [ { alertId, type, level(原样 B/Y/O/R/中文), title, pubTime } ] }
    Q_INVOKABLE QVariantMap weatherData(const QString &cityJson) const;

signals:
    void citySearchFinished(const QVariantList &cities);
    void weatherUpdated(const QString &cityId);
    void busyChanged();

private slots:
    void onPollTick();

private:
    struct CityInfo
    {
        QString cityId;
        QString name;
        QString province;
        double latitude = 0;
        double longitude = 0;
        bool isValid() const { return !cityId.isEmpty() && !name.isEmpty(); }
    };

    // 单城市内存快照（上游仅内存缓存，重启即空，行为对齐）
    struct Snapshot
    {
        bool valid = false;
        bool failed = false;    // 最近一次拉取失败（成功清除；旧数据保留降级展示）
        qint64 lastFetchAt = 0; // unix 秒
        QVariantMap current;
        QVariantMap today;
        QVariantList alerts;
    };

    static CityInfo cityFromJson(const QString &cityJson);
    void fetchNow(const CityInfo &city);
    void applySnapshot(const CityInfo &city, const QJsonObject &body);
    bool isFresh(const Snapshot &snapshot) const;
    void markFailed(const QString &cityId);

    QNetworkAccessManager *m_nam = nullptr;
    QTimer m_pollTimer;
    ConfigStore *m_configs = nullptr;
    // 轮询间隔（秒）；默认值 3600 与 WeatherService.cpp 的 kDefaultPollIntervalSecs 保持一致，
    // onPollTick 每次唤醒按 weather.poll_interval 配置 qBound(1800, …, 10800) 重读
    int m_pollIntervalSecs = 3600;
    QHash<QString, CityInfo> m_activeCities;    // key = cityId（轮询集合，request 时登记）
    QHash<QString, Snapshot> m_cache;           // key = cityId
    QHash<QString, QNetworkReply *> m_inflight; // key = cityId，防同城市并发在途
};
