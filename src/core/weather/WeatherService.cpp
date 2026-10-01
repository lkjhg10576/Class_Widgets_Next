#include "WeatherService.h"

#include "../ConfigStore.h"
#include "../Logger.h"
#include "../notification/NotificationModel.h"
#include "../notification/NotificationProvider.h"
#include "../notification/NotificationService.h"
#include "WeatherCodes.h"
#include "providers/AmapProvider.h"
#include "providers/CaiyunProvider.h"
#include "providers/QweatherProvider.h"
#include "providers/WeathercnProvider.h"
#include "providers/XiaomiProvider.h"

#include <QDateTime>
#include <QNetworkAccessManager>

namespace Log = cwn::Log; // 命名空间别名：MSVC 拒绝 using cwn::Log;（C2873）

namespace {

// 轮询参数（weather.rs:18-24）
constexpr int kDefaultPollIntervalSecs = 3600;
constexpr int kMinPollIntervalSecs = 1800;
constexpr int kMaxPollIntervalSecs = 10800;
constexpr int kPollTickMs = 60 * 1000; // IDLE_WAKE_SECS：60s 唤醒检查过期

// 冷却封顶：auth/quota 后 2×轮询间隔，至多 6h
constexpr qint64 kMaxCooldownSecs = 6 * 3600;

// 气象预警通知来源（灵动通知设置页可见/可关）
constexpr auto kAlertProviderId = "com.classwidgets.weather.alerts";

qint64 unixNow()
{
    return QDateTime::currentSecsSinceEpoch();
}

// 夜间判定：19:00–06:00 按本机时钟（仅影响图标昼夜档）
bool isNightNow()
{
    const int hour = QDateTime::currentDateTime().time().hour();
    return hour >= 19 || hour < 6;
}

} // namespace

WeatherService::WeatherService(ConfigStore *configs, NotificationService *notifications,
                               QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_configs(configs)
    , m_notifications(notifications)
{
    // 60s 唤醒 + "间隔的 90%"过期判定：上游主循环的事件驱动等价移植
    m_pollTimer.setInterval(kPollTickMs);
    connect(&m_pollTimer, &QTimer::timeout, this, &WeatherService::onPollTick);

    const auto add = [this](WeatherProvider *provider) {
        m_providers.insert(provider->id(), provider);
        connect(provider, &WeatherProvider::citySearchFinished, this,
                &WeatherService::citySearchFinished);
        connect(provider, &WeatherProvider::fetchFinished, this,
                &WeatherService::onFetchFinished);
        connect(provider, &WeatherProvider::connectionTestFinished, this,
                &WeatherService::connectionTestFinished);
    };
    add(new XiaomiProvider(m_nam, this));
    add(new AmapProvider(m_configs, m_nam, this));
    add(new QweatherProvider(m_configs, m_nam, this));
    add(new WeathercnProvider(m_configs, m_nam, this));
    add(new CaiyunProvider(m_configs, m_nam, this));

    // 气象预警通知来源：注册进灵动通知（通知设置页可开关/改走系统通知）
    if (m_notifications) {
        new NotificationProvider(QString::fromLatin1(kAlertProviderId), tr("Weather Alerts"),
                                 QStringLiteral("ic_fluent_warning_20_regular"), true,
                                 m_notifications, this);
    }
}

WeatherService::~WeatherService() = default;

QString WeatherService::widgetTypeId()
{
    return QStringLiteral("classwidgets.weather");
}

void WeatherService::start()
{
    if (!m_pollTimer.isActive())
        m_pollTimer.start();
    // 启动即拉一次：组件侧 request() 依赖 QML 加载时序（Loader 异步、城市绑定
    // 重算），首个 60s 唤醒前可能一直是空白。此处按 weather.city 主动登记活动
    // 城市并拉取，软件一启动就有数据；组件随后 request() 命中的是新鲜缓存，
    // 不会重复请求（isFresh 短路）。
    if (const auto city = configuredCity(); city.isValid()) {
        m_activeCities.insert(city.cityId, city);
        fetchNow(city, /*manual=*/false);
    }
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
            fetchNow(it.value(), /*manual=*/false);
    }
}

void WeatherService::searchCity(const QString &keyword)
{
    const QString trimmed = keyword.trimmed();
    if (trimmed.isEmpty()) {
        emit citySearchFinished({});
        return;
    }

    WeatherProvider *p = currentProvider();
    // 无搜索能力的源（彩云）复用小米搜索：结果仅经纬度被消费
    WeatherProvider *searchProvider =
        p && p->supportsCitySearch() ? p : m_providers.value(QStringLiteral("xiaomi"));
    if (searchProvider)
        searchProvider->searchCity(trimmed);
}

void WeatherService::request(const QString &cityJson)
{
    const CityInfo city = cityFromJson(cityJson);
    if (!city.isValid())
        return; // 未配置城市：组件展示占位（上游"无城市=功能静默不拉取"语义）

    m_activeCities.insert(city.cityId, city);
    const Snapshot snapshot = m_cache.value(city.cityId);
    if (!snapshot.valid || !isFresh(snapshot))
        fetchNow(city, /*manual=*/false);
}

void WeatherService::refresh(const QString &cityJson)
{
    const CityInfo city = cityFromJson(cityJson);
    if (!city.isValid())
        return;
    m_activeCities.insert(city.cityId, city);
    fetchNow(city, /*manual=*/true);
}

void WeatherService::applyConfigChange()
{
    m_cooldownUntil.clear();
    // 数据源/密钥变更后立即按新源重拉：先把 weather.city 登记进活动集合
    // （组件可能尚未 request 过；或是刚在设置页选好城市的首次拉取），
    // 再对其余活动城市用 manual 跳过过期判定与冷却，逐个重拉
    if (const auto city = configuredCity(); city.isValid())
        m_activeCities.insert(city.cityId, city);
    for (auto it = m_activeCities.constBegin(); it != m_activeCities.constEnd(); ++it)
        fetchNow(it.value(), /*manual=*/true);
    // 通知消费者重读 weatherData()：新源缺凭据（无拉取）等场景下界面也要
    // 立刻从旧源的展示态切到 unconfigured，不能等下一次拉取或 60s 轮询
    emit configChanged();
}

void WeatherService::testConnection()
{
    if (WeatherProvider *p = currentProvider())
        p->testConnection();
}

QVariantMap WeatherService::weatherData(const QString &cityJson) const
{
    QVariantMap result;
    result.insert(QStringLiteral("status"), QStringLiteral("empty"));

    const CityInfo city = cityFromJson(cityJson);
    if (!city.isValid())
        return result;
    result.insert(QStringLiteral("cityName"), city.name);

    if (WeatherProvider *p = currentProvider(); p && !p->isConfigured()) {
        // 已选城市但当前源缺凭据：组件提示去设置页配置
        result.insert(QStringLiteral("status"), QStringLiteral("unconfigured"));
        return result;
    }

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
                       cwn::weather::iconForCode(
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
    // 数据源扩展键（只增不改）：高德 adcode / 华风 Location Key
    city.adcode = obj.value(QLatin1String("adcode")).toString();
    city.wcnKey = obj.value(QLatin1String("wcnKey")).toString();
    if (!city.isValid())
        return {};
    return city;
}

WeatherService::CityInfo WeatherService::configuredCity() const
{
    if (!m_configs)
        return {};
    const auto value = m_configs->value(QStringLiteral("weather.city"));
    if (!value.has_value() || !value->isString())
        return {};
    return cityFromJson(value->toString());
}

QString WeatherService::configuredProviderId() const
{
    if (m_configs) {
        if (const auto v = m_configs->value(QStringLiteral("weather.provider"))) {
            const QString id = v->toString();
            if (!id.isEmpty())
                return id;
        }
    }
    return QStringLiteral("xiaomi");
}

WeatherProvider *WeatherService::currentProvider() const
{
    return m_providers.value(configuredProviderId(),
                             m_providers.value(QStringLiteral("xiaomi")));
}

void WeatherService::fetchNow(const CityInfo &city, bool manual)
{
    WeatherProvider *p = currentProvider();
    if (!p || !p->isConfigured())
        return; // 缺凭据（QML 显示 unconfigured 态），静默不拉取
    if (m_pendingCities.contains(city.cityId))
        return; // 同城市去重，等在途回包
    if (!manual) {
        // 冷却期内不自动重拉（auth/quota 退避）；手动刷新绕过
        if (unixNow() < m_cooldownUntil.value(p->id(), 0))
            return;
    }

    m_pendingCities.insert(city.cityId);
    ++m_pendingFetches;
    emit busyChanged();
    p->fetch(city);
}

void WeatherService::onFetchFinished(const QString &cityId,
                                     const WeatherProvider::Snapshot &snapshot,
                                     const QString &errorKind)
{
    m_pendingCities.remove(cityId);
    --m_pendingFetches;
    emit busyChanged();

    WeatherProvider *p = qobject_cast<WeatherProvider *>(sender());
    if (p != currentProvider()) {
        // 数据源已切换：丢弃旧源在途回包；该城市仍活动时立即按新源补拉
        if (m_activeCities.contains(cityId))
            fetchNow(m_activeCities.value(cityId), /*manual=*/true);
        return;
    }

    if (!errorKind.isEmpty()) {
        if (errorKind == QLatin1String("auth") || errorKind == QLatin1String("quota")) {
            m_cooldownUntil.insert(p->id(),
                                   unixNow()
                                       + qMin<qint64>(2 * m_pollIntervalSecs, kMaxCooldownSecs));
        }
        Log::warn(QStringLiteral("Weather fetch failed for %1 (%2): %3")
                      .arg(cityId, p->id(), errorKind));
        markFailed(cityId);
        return;
    }
    if (!snapshot.valid) {
        markFailed(cityId);
        return;
    }

    Snapshot &cached = m_cache[cityId];
    cached.valid = true;
    cached.failed = false;
    cached.lastFetchAt = unixNow();
    cached.current = snapshot.current;
    cached.today = snapshot.today;
    cached.alerts = snapshot.alerts;
    pushTopAlert(snapshot.alerts);

    Log::info(QStringLiteral("Weather updated for %1 (%2, current=%3, alerts=%4)")
                  .arg(cityId, p->id(),
                       snapshot.current.isEmpty() ? QStringLiteral("none")
                                                  : QStringLiteral("ok"))
                  .arg(snapshot.alerts.size()));
    emit weatherUpdated(cityId);
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

void WeatherService::pushTopAlert(const QVariantList &alerts)
{
    if (!m_notifications || alerts.isEmpty())
        return;

    // 一次拉取新增多条预警时只推最高等级（产品决策）；会话内按 alertId 去重，
    // 重启后会重推当前生效预警。同一预警内容更新不重推（已知限制）。
    QVariantMap top;
    int topRank = -1;
    for (const QVariant &item : alerts) {
        const QVariantMap alert = item.toMap();
        const QString alertId = alert.value(QStringLiteral("alertId")).toString();
        if (alertId.isEmpty() || m_pushedAlertIds.contains(alertId))
            continue;
        const int rank = cwn::weather::alertLevelRank(alert.value(QStringLiteral("level")).toString());
        if (top.isEmpty() || rank > topRank) {
            top = alert;
            topRank = rank;
        }
    }
    if (top.isEmpty())
        return;
    m_pushedAlertIds.insert(top.value(QStringLiteral("alertId")).toString());

    cwn::notification::NotificationData data;
    data.providerId = QString::fromLatin1(kAlertProviderId);
    data.level = cwn::notification::LevelWarning;
    data.title = top.value(QStringLiteral("title")).toString();
    if (data.title.isEmpty())
        data.title = top.value(QStringLiteral("type")).toString();
    data.message = top.value(QStringLiteral("type")).toString();
    data.icon = QStringLiteral("ic_fluent_warning_20_regular");
    data.duration = 8000;
    data.closable = true;
    m_notifications->dispatch(data);
}
