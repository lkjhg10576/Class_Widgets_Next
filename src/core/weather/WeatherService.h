#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include "WeatherProvider.h"

class ConfigStore;
class NotificationService;
class QNetworkAccessManager;

// 天气小组件数据门面：持有多数据源 Provider（小米/高德/和风/华风爱科/彩云），
// 按 weather.provider 配置分发（默认 xiaomi）；负责 60s 轮询、按"间隔的 90%"
// 过期重拉、同城市在途去重、内存快照缓存，以及气象预警 → 灵动通知推送
// （NotificationProvider id "com.classwidgets.weather.alerts"，一次拉取新增
// 多条时只推最高等级，会话内按 alertId 去重）。
//
// 数据源切换/密钥变更由设置页调 applyConfigChange() 即时生效；auth/quota 类
// 错误使该源进入冷却（2×轮询间隔，封顶 6h），冷却期内仅手动刷新可重拉。
//
// QML 数值契约（weatherData() 返回，Provider 归一后语义稳定）：
//   { status: "empty"|"unconfigured"|"stale"|"ok", cityName, lastFetchAt,
//     current: { temperature, weatherCode, feelsLike?, humidity, windSpeed?,
//                windScale?(高德：风力等级文本), pm25?, aqi?(仅小米), icon },
//     today:   { tempMax, tempMin, dayCode, nightCode, precipProb?, sunrise?, sunset? },
//     alerts:  [ { alertId, type, level("B"|"Y"|"O"|"R"), levelRaw, title, pubTime } ] }
// status 语义：empty=未配置城市；unconfigured=已选城市但当前源缺凭据；
// stale=最近一次拉取失败（旧数据降级展示）；ok=有效快照。alerts 仍随契约返回
// （供调试/未来消费），组件本体不再渲染预警（预警走灵动通知）。
//
// 线程模型：全部回调经 QNetworkAccessManager/QTimer 在 GUI 线程事件驱动。
class WeatherService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit WeatherService(ConfigStore *configs, NotificationService *notifications,
                            QObject *parent = nullptr);
    ~WeatherService() override;

    // BuiltinWidgets 注册表中的 widget_id；AppCentral 以此为天气组件挂专属 backend
    static QString widgetTypeId();

    // AppCentral 在 configs->load() 之后调用：启动 60s 轮询 tick
    void start();

    // 停止 60s 轮询 tick（extensions-feature-plan §5 B4：天气扩展关闭时调用）。
    // 与 start() 对称；本类全部逻辑在 GUI 线程事件循环内（见类注释线程模型），
    // 停 QTimer 即停全部自动重拉，无需其他状态清理。在途网络回包仍正常入缓存
    // （QTimer 停止不影响已发出的请求），期间无组件消费，weatherData() 按
    // 缓存原样返回 empty/unconfigured/stale 契约状态，不崩。
    void stop() { m_pollTimer.stop(); }

    bool busy() const { return m_pendingFetches > 0; }

    // --- QML 契约 ---
    Q_INVOKABLE void searchCity(const QString &keyword); // 异步 → citySearchFinished
    Q_INVOKABLE void request(const QString &cityJson);   // 缺数据或已过期才拉取
    Q_INVOKABLE void refresh(const QString &cityJson);   // 忽略过期时间强制拉取
    Q_INVOKABLE QVariantMap weatherData(const QString &cityJson) const;
    // 设置页在数据源/密钥变更后调用：清冷却并立即按新源重拉所有活动城市
    Q_INVOKABLE void applyConfigChange();
    // 设置页"测试连接"：对当前源发一次最小请求 → connectionTestFinished
    Q_INVOKABLE void testConnection();

signals:
    void citySearchFinished(const QVariantList &cities);
    void weatherUpdated(const QString &cityId);
    void busyChanged();
    void connectionTestFinished(bool ok, const QString &errorKind);

private slots:
    void onPollTick();

private:
    using CityInfo = WeatherProvider::CityInfo;

    // 单城市内存快照（仅内存缓存，重启即空，行为对齐上游）
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
    QString configuredProviderId() const;
    WeatherProvider *currentProvider() const;
    void fetchNow(const CityInfo &city, bool manual);
    void onFetchFinished(const QString &cityId, const WeatherProvider::Snapshot &snapshot,
                         const QString &errorKind);
    bool isFresh(const Snapshot &snapshot) const;
    void markFailed(const QString &cityId);
    void pushTopAlert(const QVariantList &alerts);

    QNetworkAccessManager *m_nam = nullptr;
    QTimer m_pollTimer;
    ConfigStore *m_configs = nullptr;
    NotificationService *m_notifications = nullptr;
    // 轮询间隔（秒）；默认值 3600 与 onPollTick 的 qBound(1800, …, 10800) 一致，
    // 每次唤醒按 weather.poll_interval 配置重读
    int m_pollIntervalSecs = 3600;
    QHash<QString, WeatherProvider *> m_providers; // id → provider（xiaomi/amap/…）
    QHash<QString, CityInfo> m_activeCities;       // key = cityId（轮询集合，request 时登记）
    QHash<QString, Snapshot> m_cache;              // key = cityId
    QSet<QString> m_pendingCities;                 // 在途去重（fetch 期间）
    int m_pendingFetches = 0;                      // busy 统计
    QHash<QString, qint64> m_cooldownUntil;        // providerId → 冷却截止（unix 秒）
    QSet<QString> m_pushedAlertIds;                // 会话内预警去重（alertId）
};
