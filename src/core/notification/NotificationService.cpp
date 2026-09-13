#include "NotificationService.h"

#include "NotificationProvider.h"

#include "../AppPaths.h"
#include "../ConfigStore.h"
#include "../Logger.h"
#include "../NativeFileDialog.h" // B4：原生文件对话框替代 QFileDialog（去 Qt6::Widgets）

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonValue>
#include <QTimer>
#include <QVariantList>
#include <optional>

#ifdef Q_OS_WIN
// service.py:181-200 上游用 QtMultimedia 的 QSoundEffect 播放音效；本项目不引入
// QtMultimedia，按任务约束映射为 Win32 PlaySound（winmm.lib）。
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#endif

using cwn::notification::NotificationData;
using cwn::notification::defaultLevelSound;
using cwn::notification::toPayload;

NotificationService *NotificationService::s_defaultInstance = nullptr;

namespace {

// 上游 runtime.py 注册的 5 个课程表通知 provider（runtime.py:69-113）与
// utils/backend.py 注册的调试 provider（backend.py:60-67）。id/name/icon 逐字一致。
struct ProviderSpec
{
    const char *id;
    const char *name;      // QCoreApplication translate 上下文 "NotificationProviders"
    const char *icon;
};

constexpr ProviderSpec kProviderSpecs[] = {
    { "com.classwidgets.schedule.runtime.class", "Class Notifications",
      "ic_fluent_book_clock_20_regular" },                        // runtime.py:70-76
    { "com.classwidgets.schedule.runtime.activity", "Activity Notifications",
      "ic_fluent_broad_activity_feed_20_regular" },               // runtime.py:80-86
    { "com.classwidgets.schedule.runtime.break", "Break Notifications",
      "ic_fluent_drink_coffee_20_regular" },                      // runtime.py:89-95
    { "com.classwidgets.schedule.runtime.free", "Free Time Notifications",
      "ic_fluent_person_running_20_regular" },                    // runtime.py:98-104
    { "com.classwidgets.schedule.runtime.preparation", "Preparation Bell",
      "ic_fluent_alert_urgent_20_regular" },                      // runtime.py:107-113
    { "com.classwidgets.debug", "Debug Notification",
      "ic_fluent_code_20_regular" },                              // backend.py:60-67
};

QString providerName(const ProviderSpec &spec)
{
    return QCoreApplication::translate("NotificationProviders", spec.name);
}

// 配置读取辅助（点分路径；缺键时返回默认值，对齐 config/model.py:249-266）
std::optional<QJsonValue> readConfig(const ConfigStore *configs, const QString &key)
{
    return configs ? configs->value(key) : std::nullopt;
}

bool readBool(const ConfigStore *configs, const QString &key, bool defaultValue)
{
    if (auto v = readConfig(configs, key))
        return v->toBool(defaultValue);
    return defaultValue;
}

int readInt(const ConfigStore *configs, const QString &key, int defaultValue)
{
    if (auto v = readConfig(configs, key))
        return v->toInt(defaultValue);
    return defaultValue;
}

double readDouble(const ConfigStore *configs, const QString &key, double defaultValue)
{
    if (auto v = readConfig(configs, key))
        return v->toDouble(defaultValue);
    return defaultValue;
}

QString readString(const ConfigStore *configs, const QString &key, const QString &defaultValue = {})
{
    if (auto v = readConfig(configs, key))
        return v->toString(defaultValue);
    return defaultValue;
}

QString providerConfigKey(const QString &providerId, const QString &field)
{
    return QStringLiteral("notifications.providers.%1.%2").arg(providerId, field);
}

} // namespace

NotificationService::NotificationService(ConfigStore *configs, QObject *parent)
    : QObject(parent)
    , m_configs(configs)
{
    s_defaultInstance = this;

    // 上游由 ScheduleRuntime（runtime.py:66-116）、UtilsBackend（backend.py:57-70）、
    // 插件广场等各自创建 Provider；C++ 侧 M4 阶段调度方尚未建 Provider，
    // 由服务统一预注册（id/name/icon/use_system_notify 与上游逐字一致，
    // use_system_notify 能力均为 true）。后续插件体系（Phase 2）可继续
    // registerProvider 扩展。
    for (const ProviderSpec &spec : kProviderSpecs) {
        // 注册即挂父对象（自动释放）；use_system_notify=True（runtime.py:75 等）
        new NotificationProvider(QString::fromLatin1(spec.id), providerName(spec),
                                 QString::fromLatin1(spec.icon), true, this, this);
    }
}

NotificationService::~NotificationService()
{
    if (s_defaultInstance == this)
        s_defaultInstance = nullptr;
}

// ── provider 注册（manager.py:31-47）──────────────────────────────

void NotificationService::registerProvider(NotificationProvider *provider)
{
    if (!provider || provider->id().isEmpty()) {
        // manager.py:32-34：无 id/name 的无效注册
        cwn::Log::warn(QStringLiteral("Invalid notification provider registration"));
        return;
    }
    m_providers.insert(provider->id(), provider);
    if (!m_providerOrder.contains(provider->id()))
        m_providerOrder.append(provider->id());
    // manager.py:37 `_ = provider.get_config()`：注册即读取配置（无副作用），省略
    cwn::Log::debug(QStringLiteral("Registered notification provider: %1").arg(provider->id()));
    emitProvidersChanged();
}

void NotificationService::unregisterProvider(const QString &providerId)
{
    const qsizetype removedCount = m_providers.remove(providerId);
    if (removedCount > 0) {
        // manager.py:39-43
        m_providerOrder.removeAll(providerId);
        cwn::Log::debug(QStringLiteral("Unregistered notification provider: %1").arg(providerId));
        emitProvidersChanged();
    }
}

bool NotificationService::isProviderEnabled(const QString &providerId) const
{
    // manager.py:45-47：未配置视为启用
    return readBool(m_configs, providerConfigKey(providerId, QStringLiteral("enabled")), true);
}

QVariantMap NotificationService::providerConfig(const QString &providerId) const
{
    // provider.py:49-56 + model.py:40-43 NotificationProviderConfig 默认值
    return {
        { QStringLiteral("enabled"),
          readBool(m_configs, providerConfigKey(providerId, QStringLiteral("enabled")), true) },
        { QStringLiteral("use_system_notify"),
          readBool(m_configs, providerConfigKey(providerId, QStringLiteral("use_system_notify")), false) },
        { QStringLiteral("use_app_notify"),
          readBool(m_configs, providerConfigKey(providerId, QStringLiteral("use_app_notify")), true) },
    };
}

// ── 分发（manager.py:88-142）──────────────────────────────────────

void NotificationService::dispatch(const NotificationData &data)
{
    // manager.py:90
    cwn::Log::info(QStringLiteral("Dispatching notification: %1 - %2 (Level: %3)")
                       .arg(data.providerId, data.title).arg(data.level));

    // manager.py:92-95：按 provider_id 取配置（缺省即默认配置）
    const QVariantMap cfg = providerConfig(data.providerId);
    const bool cfgEnabled = cfg.value(QStringLiteral("enabled"), true).toBool();
    const bool cfgUseSystem = cfg.value(QStringLiteral("use_system_notify"), false).toBool();
    const bool cfgUseApp = cfg.value(QStringLiteral("use_app_notify"), true).toBool();

    if (!notificationsEnabled()) // manager.py:97-98 全局开关
        return;
    if (!cfgEnabled) // manager.py:100-101 provider 开关
        return;

    QVariantMap payload = toPayload(data);
    payload.insert(QStringLiteral("use_system"), cfgUseSystem); // manager.py:106

    if (!cfgUseSystem && !cfgUseApp) // manager.py:109-110 两路都关 → 丢弃
        return;

    // manager.py:112-116：`use_system_notify and (provider_use_system or
    // use_system_notify)` 化简后恒等于 cfg 开关本身（上游原样的化简重述），
    // 故 provider 的 use_system_notify 能力标记不影响分发结果。
    if (cfgUseSystem) {
        // manager.py:117-123：上游调 tray_icon.push_notification(title, message or "")；
        // C++ 侧经信号由主控接到 TrayIcon（QSystemTrayIcon::showMessage）
        emit systemNotificationRequested(data.title, data.message.isEmpty() ? QString() : data.message);
    }

    if (cfgUseApp) { // manager.py:128-142 应用内通知
        if (!m_qmlReady) {
            cwn::Log::debug(QStringLiteral("QML not ready, queuing notification: %1").arg(data.title));
            m_pendingNotifications.append(payload);
        } else {
            emit notified(payload);
        }

        if (!data.silent) {
            // manager.py:137-140：上游经 utils_backend.playNotificationSound 委托
            // 到 NotificationService；本类合并后直接调用
            playNotificationSound(data.providerId, data.level);
        }
    }
}

// ── 状态变化分发（runtime.py:302-443 _update_notify）──────────────

void NotificationService::dispatchStatusChange(const QString &status,
                                               const QVariantMap &currentEntry,
                                               const QVariantMap &currentSubject,
                                               const QVariantList &nextEntries,
                                               const QVariantList &subjects)
{
    // runtime.py:303-305 `previous_entry != current_entry`：以"日期+状态+条目
    // 指纹"复刻该判定。currentsChanged 信号本身已按条目变化发射
    // （ScheduleRuntime.cpp:474-475），此去重保证同一状态迁移只发一次，
    // 主控误接 updated() 时也不会刷屏。
    const int timeOffset = readInt(m_configs, QStringLiteral("schedule.time_offset"), 0);
    const QDate offsetDate = QDateTime::currentDateTime().addSecs(timeOffset).date();
    const QString fingerprint = offsetDate.toString(Qt::ISODate) + QLatin1Char('|') + status
        + QLatin1Char('|') + currentEntry.value(QStringLiteral("startTime")).toString()
        + QLatin1Char('|') + currentEntry.value(QStringLiteral("title")).toString()
        + QLatin1Char('|') + currentEntry.value(QStringLiteral("subjectId")).toString();
    if (fingerprint == m_lastStatusKey)
        return;
    m_lastStatusKey = fingerprint;

    // runtime.py:308-414 分发表：按状态产出标题/正文（翻译上下文与源串逐字一致）
    QString title;
    QString message; // QString()（null）对应上游 message = None
    if (status == QLatin1String("class")) {
        // runtime.py:308-323
        title = QCoreApplication::translate("ScheduleRuntime", "Class Started");
        const QString entryTitle = currentEntry.value(QStringLiteral("title")).toString();
        if (!entryTitle.isEmpty()) {
            message = entryTitle; // runtime.py:313-315 优先条目标题
        } else {
            // runtime.py:316-323 其次科目名 + 教师
            const QString subjectName = currentSubject.value(QStringLiteral("name")).toString();
            if (!subjectName.isEmpty()) {
                message = subjectName;
                const QString teacher = currentSubject.value(QStringLiteral("teacher")).toString();
                if (!teacher.isEmpty())
                    message += QStringLiteral(" —— ") + teacher; // runtime.py:323
            }
        }
    } else if (status == QLatin1String("activity")) {
        // runtime.py:325-332：活动只用条目标题，无标题则正文为空
        title = QCoreApplication::translate("ScheduleRuntime", "Activity Started");
        const QString entryTitle = currentEntry.value(QStringLiteral("title")).toString();
        if (!entryTitle.isEmpty())
            message = entryTitle;
    } else if (status == QLatin1String("preparation") && !nextEntries.isEmpty()) {
        // runtime.py:334-367（无 next_entries 时落到兜底分支，保持上游语义）
        title = QCoreApplication::translate("ScheduleRuntime", "Intermission");
        message = nextEntryMessage(nextEntries.first().toMap(), subjects, false);
    } else if (status == QLatin1String("break")) {
        // runtime.py:369-405
        title = QCoreApplication::translate("ScheduleRuntime", "Recess");
        if (!nextEntries.isEmpty())
            message = nextEntryMessage(nextEntries.first().toMap(), subjects, false);
        else
            message = QCoreApplication::translate("ScheduleRuntime", "Enjoy your break");
    } else if (status == QLatin1String("free")) {
        // runtime.py:407-409
        title = QCoreApplication::translate("ScheduleRuntime", "Free Time");
    } else {
        // runtime.py:411-414 其他状态兜底
        title = QCoreApplication::translate("ScheduleRuntime", "Status Changed");
        message = QCoreApplication::translate("ScheduleRuntime", "Current status: %1").arg(status);
    }

    // runtime.py:416-427 按状态选 provider
    QString providerId;
    if (status == QLatin1String("class"))
        providerId = QStringLiteral("com.classwidgets.schedule.runtime.class");
    else if (status == QLatin1String("activity"))
        providerId = QStringLiteral("com.classwidgets.schedule.runtime.activity");
    else if (status == QLatin1String("preparation") || status == QLatin1String("break"))
        providerId = QStringLiteral("com.classwidgets.schedule.runtime.break");
    else if (status == QLatin1String("free"))
        providerId = QStringLiteral("com.classwidgets.schedule.runtime.free");
    else
        providerId = QStringLiteral("com.classwidgets.schedule.runtime.class"); // fallback

    // runtime.py:429-440 NotificationData(level=ANNOUNCEMENT, duration=5000, closable=True)
    NotificationData data;
    data.providerId = providerId;
    data.level = cwn::notification::LevelAnnouncement;
    data.title = title;
    data.message = message;
    data.duration = 5000;
    data.closable = true;
    if (NotificationProvider *provider = m_providers.value(providerId))
        data.icon = provider->icon(); // provider.py:78 传递 Provider 图标

    dispatch(data);
}

void NotificationService::dispatchStatusChange(const QString &status)
{
    // 便捷重载：从注入的运行时对象动态读取属性（避免依赖 schedule/ 头文件）
    if (!m_runtimeSource) {
        cwn::Log::warn(QStringLiteral("dispatchStatusChange(%1) ignored: no schedule "
                                      "runtime source injected (setScheduleRuntimeSource)")
                           .arg(status));
        return;
    }
    dispatchStatusChange(status,
                         m_runtimeSource->property("currentEntry").toMap(),
                         m_runtimeSource->property("currentSubject").toMap(),
                         m_runtimeSource->property("nextEntries").toList(),
                         m_runtimeSource->property("subjects").toList());
}

void NotificationService::setScheduleRuntimeSource(QObject *runtimeSource)
{
    m_runtimeSource = runtimeSource;
}

// ── 预备铃（runtime.py:445-496）──────────────────────────────────

void NotificationService::checkPreparationBell()
{
    if (!m_runtimeSource)
        return;

    // runtime.py:446-449：预备状态下且存在后续条目
    const QVariantList nextEntries = m_runtimeSource->property("nextEntries").toList();
    const QVariantList subjects = m_runtimeSource->property("subjects").toList();
    const QString status = m_runtimeSource->property("currentStatus").toString();
    if (nextEntries.isEmpty() || status != QLatin1String("preparation"))
        return;

    const QVariantMap nextEntry = nextEntries.first().toMap();
    const QString startHm = nextEntry.value(QStringLiteral("startTime")).toString();
    if (startHm.isEmpty())
        return;

    // runtime.py:454：preparation_time（getattr(...) or 2 → 0/负值视为 2）
    int prepMin = readInt(m_configs, QStringLiteral("schedule.preparation_time"), 2);
    if (prepMin <= 0)
        prepMin = 2;

    // runtime.py:452-456：当前时间（含 time_offset，微秒归零）必须与
    // "下一节开始 - preparation_time 分钟" 整秒相等才触发
    const int timeOffset = readInt(m_configs, QStringLiteral("schedule.time_offset"), 0);
    const QDateTime now = QDateTime::currentDateTime().addSecs(timeOffset);
    const QTime start = QTime::fromString(startHm, QStringLiteral("HH:mm"));
    if (!start.isValid()) {
        cwn::Log::warn(QStringLiteral("Preparation bell: invalid next entry startTime '%1'").arg(startHm));
        return;
    }
    const QDateTime nextStart(now.date(), start); // runtime.py:452-453（同日组合，上游一致）
    const QDateTime target = nextStart.addSecs(-prepMin * 60);
    const QDateTime nowSec(now.date(),
                           QTime(now.time().hour(), now.time().minute(), now.time().second()));
    if (target != nowSec)
        return;

    // 同一目标分钟只发一次（updated() 每秒/课表刷新都会触发，上游在
    // 同一秒内刷新课表会重复发送，这里保守去重）
    const QString key = startHm + QLatin1Char('|') + target.time().toString(QStringLiteral("HH:mm"));
    if (key == m_lastBellKey)
        return;
    m_lastBellKey = key;

    // runtime.py:457-480："Coming up: ..." 消息
    const QString message = nextEntryMessage(nextEntry, subjects, true);

    // runtime.py:482-493：预备铃通知（ANNOUNCEMENT / 5000ms / 可关闭）
    NotificationData data;
    data.providerId = QStringLiteral("com.classwidgets.schedule.runtime.preparation");
    data.level = cwn::notification::LevelAnnouncement;
    data.title = QCoreApplication::translate("ScheduleRuntime", "Preparation Bell");
    data.message = message;
    data.duration = 5000;
    data.closable = true;
    if (NotificationProvider *provider = m_providers.value(data.providerId))
        data.icon = provider->icon();

    dispatch(data);
}

// runtime.py:338-362 / 374-398 / 457-480 三处重复的"下一节科目"消息构造合并实现。
// comingUp=false → "Next: ..."；comingUp=true → "Coming up: ..."
QString NotificationService::nextEntryMessage(const QVariantMap &nextEntry,
                                              const QVariantList &subjects,
                                              bool comingUp) const
{
    // service.py:171-177 get_subject：按 subjectId 在课表科目里查找
    const QString subjectId = nextEntry.value(QStringLiteral("subjectId")).toString();
    QVariantMap subject;
    if (!subjectId.isEmpty()) {
        for (const QVariant &s : subjects) {
            const QVariantMap candidate = s.toMap();
            if (candidate.value(QStringLiteral("id")).toString() == subjectId) {
                subject = candidate;
                break;
            }
        }
    }

    const QString subjectName = subject.value(QStringLiteral("name")).toString();
    if (!subjectName.isEmpty()) {
        // runtime.py:349-358：本地教室 → Next: {name}；否则带地点/标注
        const bool isLocal = subject.value(QStringLiteral("isLocalClassroom"), QVariant(true)).toBool();
        if (isLocal)
            return comingUp ? QCoreApplication::translate("ScheduleRuntime", "Coming up: %1").arg(subjectName)
                            : QCoreApplication::translate("ScheduleRuntime", "Next: %1").arg(subjectName);

        const QString location = subject.value(QStringLiteral("location")).toString();
        if (!location.isEmpty())
            return comingUp ? QCoreApplication::translate("ScheduleRuntime", "Coming up: %1 at %2").arg(subjectName, location)
                            : QCoreApplication::translate("ScheduleRuntime", "Next: %1 at %2").arg(subjectName, location);
        return comingUp ? QCoreApplication::translate("ScheduleRuntime", "Coming up: %1 (Off-site)").arg(subjectName)
                        : QCoreApplication::translate("ScheduleRuntime", "Next: %1 (Off-site)").arg(subjectName);
    }

    // runtime.py:360-362 / 396-398 / 476-478：科目缺失时回退条目标题
    const QString nextTitle = nextEntry.value(QStringLiteral("title")).toString();
    if (!nextTitle.isEmpty())
        return comingUp ? QCoreApplication::translate("ScheduleRuntime", "Coming up: %1").arg(nextTitle)
                        : QCoreApplication::translate("ScheduleRuntime", "Next: %1").arg(nextTitle);
    return QString(); // 上游 message = None
}

// ── QML 契约（manager.py:50-86）──────────────────────────────────

void NotificationService::notifyQmlReady()
{
    // manager.py:50-73 set_qml_ready(True)：就绪后补发全部排队通知
    m_qmlReady = true;
    if (m_pendingNotifications.isEmpty())
        return;

    cwn::Log::info(QStringLiteral("QML ready, flushing %1 pending notifications")
                       .arg(m_pendingNotifications.size()));
    const QVector<QVariantMap> pending = m_pendingNotifications;
    m_pendingNotifications.clear();
    for (const QVariantMap &payload : pending)
        emit notified(payload);
}

// ── 设置页服务面（service.py 全量）────────────────────────────────

QVariantList NotificationService::notificationProviders() const
{
    // manager.py:146-165 get_providers：字段名照上游（useSystemNotify/useAppNotify
    // 为驼峰，Notification.qml:347-348 按此读取）
    QVariantList list;
    list.reserve(m_providerOrder.size());
    // 按注册顺序输出（对应上游 dict 的插入序），运行时 5 个 + 调试 provider
    for (const QString &providerId : m_providerOrder) {
        const NotificationProvider *provider = m_providers.value(providerId);
        if (!provider)
            continue;
        const QVariantMap cfg = providerConfig(provider->id());
        list.append(QVariantMap{
            { QStringLiteral("id"), provider->id() },
            { QStringLiteral("name"), provider->name().isEmpty() ? QStringLiteral("Unknown Provider")
                                                                 : provider->name() },
            { QStringLiteral("icon"), provider->icon() },
            { QStringLiteral("enabled"), cfg.value(QStringLiteral("enabled"), true).toBool() },
            { QStringLiteral("useSystemNotify"),
              cfg.value(QStringLiteral("use_system_notify"), false).toBool() },
            { QStringLiteral("useAppNotify"),
              cfg.value(QStringLiteral("use_app_notify"), true).toBool() },
        });
    }
    return list;
}

void NotificationService::setProviderConfigField(const QString &providerId,
                                                 const QString &field, const QJsonValue &value)
{
    // service.py:36-42 等：上游先补建 NotificationProviderConfig() 默认对象再赋值；
    // 写叶子键得到等价 JSON 树。经 ConfigStore::set 走锁定/类型校验。
    if (m_configs)
        m_configs->set(providerConfigKey(providerId, field), value.toVariant());
    emitProvidersChanged();
}

void NotificationService::setNotificationProviderEnabled(const QString &providerId, bool enabled)
{
    setProviderConfigField(providerId, QStringLiteral("enabled"), QJsonValue(enabled));
}

void NotificationService::setNotificationProviderSystemNotify(const QString &providerId, bool useSystem)
{
    setProviderConfigField(providerId, QStringLiteral("use_system_notify"), QJsonValue(useSystem));
}

void NotificationService::setNotificationProviderAppNotify(const QString &providerId, bool useApp)
{
    setProviderConfigField(providerId, QStringLiteral("use_app_notify"), QJsonValue(useApp));
}

void NotificationService::setLevelSound(int level, const QString &sound)
{
    // service.py:63-69：level_sounds 键在 JSON 里为字符串（"0".."3"）
    if (m_configs)
        m_configs->set(QStringLiteral("notifications.level_sounds.%1").arg(level), sound);
}

QString NotificationService::getLevelSound(int level) const
{
    // service.py:71-77：同时兼容整数键与字符串键（JSON 键恒为字符串）
    return readString(m_configs, QStringLiteral("notifications.level_sounds.%1").arg(level));
}

double NotificationService::getNotificationVolume() const
{
    // service.py:79-82；config/model.py:255 默认 0.7
    return readDouble(m_configs, QStringLiteral("notifications.volume"), 0.7);
}

void NotificationService::setNotificationVolume(double volume)
{
    if (m_configs)
        m_configs->set(QStringLiteral("notifications.volume"), volume);
}

void NotificationService::setNotificationsEnabled(bool enabled)
{
    // service.py:89-92
    if (m_configs)
        m_configs->set(QStringLiteral("notifications.enabled"), enabled);
}

bool NotificationService::getNotificationsEnabled() const
{
    // service.py:94-97；config/model.py:253 默认 True
    return readBool(m_configs, QStringLiteral("notifications.enabled"), true);
}

QString NotificationService::getNotificationProviderLevelSound(const QString &providerId, int level) const
{
    // service.py:100-108：忽略 provider_id，读全局级别音效
    Q_UNUSED(providerId);
    return getLevelSound(level);
}

void NotificationService::setNotificationProviderLevelSound(const QString &providerId, int level,
                                                            const QString &sound)
{
    // service.py:105-118：忽略 provider_id，写全局级别音效
    Q_UNUSED(providerId);
    setLevelSound(level, sound);
}

QString NotificationService::getGlobalLevelSound(int level) const
{
    // service.py:110-113
    return getLevelSound(level);
}

void NotificationService::setGlobalLevelSound(int level, const QString &sound)
{
    // service.py:115-118
    setLevelSound(level, sound);
}

double NotificationService::getGlobalVolume() const
{
    // service.py:120-123
    return getNotificationVolume();
}

void NotificationService::setGlobalVolume(double volume)
{
    // service.py:125-128
    setNotificationVolume(volume);
}

double NotificationService::getGlobalNotificationVolume() const
{
    // service.py:130-133
    return getNotificationVolume();
}

void NotificationService::setGlobalNotificationVolume(double volume)
{
    // service.py:135-138
    setNotificationVolume(volume);
}

void NotificationService::playNotificationSoundLevel(int level)
{
    // service.py:141-144：使用全局配置播放
    playNotificationSound(QStringLiteral("global"), level);
}

void NotificationService::playNotificationSound(const QString &providerId, int level)
{
    // service.py:146-203 playNotificationSound 的 Win32 移植
    if (!notificationsEnabled()) // service.py:150-151
        return;

    // service.py:153-156：自定义级别音效（JSON 键为字符串）
    const QString customSound = levelSound(level);

    // service.py:158-175：自定义路径（绝对直接用，相对挂到 assets/audio），
    // 否则按级别取默认音效
    const QString audioDir = AppPaths::instance().assetsRoot() + QStringLiteral("/audio");
    QString soundFile;
    if (!customSound.isEmpty()) {
        const QFileInfo customInfo(customSound);
        soundFile = customInfo.isAbsolute() ? customSound : audioDir + QLatin1Char('/') + customSound;
    } else {
        soundFile = audioDir + QLatin1Char('/') + defaultLevelSound(level);
    }

    // service.py:177-179：文件缺失时静默降级（补日志便于排查）
    if (!QFileInfo::exists(soundFile)) {
        cwn::Log::warn(QStringLiteral("Notification sound missing, skipped: %1 (provider %2, level %3)")
                           .arg(soundFile, providerId).arg(level));
        return;
    }

    // service.py:183-189 的 effect_key 缓存是 QSoundEffect 复用句柄的手段，
    // PlaySound 无需缓存；service.py:192-197 的 setVolume 语义由
    // applyPlaybackVolume 尽力映射（PlaySound 本身无音量参数）
    applyPlaybackVolume(notificationVolume());

#ifdef Q_OS_WIN
    // SND_ASYNC：不阻塞主线程；SND_NODEFAULT：文件格式异常时不退回系统提示音
    PlaySoundW(reinterpret_cast<const wchar_t *>(soundFile.utf16()), nullptr,
               SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
#else
    cwn::Log::warn(QStringLiteral("Sound playback is only implemented on Windows: %1").arg(soundFile));
#endif
}

bool NotificationService::selectNotificationSound(int level)
{
    // service.py:205-235 selectNotificationSound：选文件 → 复制进 assets/audio →
    // 以相对文件名保存为该级别音效
    const QString path = NativeFileDialog::getOpenFileName(
        QStringLiteral("Select Notification Sound"), QString(),
        QStringLiteral("Audio Files (*.wav *.mp3 *.ogg)"));
    if (path.isEmpty())
        return false;

    const QFileInfo sourceInfo(path);
    const QString audioDir = AppPaths::instance().assetsRoot() + QStringLiteral("/audio");
    if (!QDir().mkpath(audioDir)) {
        cwn::Log::error(QStringLiteral("Cannot create audio directory: %1").arg(audioDir));
        return false;
    }

    const QString target = audioDir + QLatin1Char('/') + sourceInfo.fileName();
    // shutil.copy2 语义：同名覆盖
    if (QFileInfo::exists(target) && !QFile::remove(target)) {
        cwn::Log::warn(QStringLiteral("Cannot overwrite existing sound: %1").arg(target));
        return false;
    }
    if (!QFile::copy(path, target)) {
        cwn::Log::error(QStringLiteral("Failed to copy notification sound %1 -> %2").arg(path, target));
        return false;
    }

    setLevelSound(level, sourceInfo.fileName());
    cwn::Log::debug(QStringLiteral("Copied notification sound to %1 (level %2)").arg(target).arg(level));
    return true;
}

void NotificationService::retranslateProviders()
{
    // runtime.py:118-135：上游 retranslate 时注销重建 provider 以刷新翻译名；
    // C++ 侧原地改名 + 通知变更，用户可见效果一致
    for (const ProviderSpec &spec : kProviderSpecs) {
        if (NotificationProvider *provider = m_providers.value(QString::fromLatin1(spec.id)))
            provider->setName(providerName(spec));
    }
    emitProvidersChanged();
}

// ── 内部辅助 ─────────────────────────────────────────────────────

bool NotificationService::notificationsEnabled() const
{
    return getNotificationsEnabled();
}

double NotificationService::notificationVolume() const
{
    return getNotificationVolume();
}

QString NotificationService::levelSound(int level) const
{
    return getLevelSound(level);
}

void NotificationService::emitProvidersChanged()
{
    emit notificationProvidersChanged();
}

void NotificationService::applyPlaybackVolume(double volume)
{
#ifdef Q_OS_WIN
    // service.py:195-197 effect.setVolume(volume) 的语义映射。PlaySound 无音量
    // 参数，退而求其次：播放前缩放 wave 输出设备音量，约 5 秒（长于内置音效
    // 时长）后恢复用户原值。重叠播放只保存/恢复一次（m_volumeAdjusted 守卫），
    // 世代号防止旧的恢复定时器覆盖新一轮缩放。该调整作用于系统 wave 设备，
    // 属尽力而为的近似。
    DWORD saved = 0;
    if (waveOutGetVolume(0, &saved) != MMSYSERR_NOERROR)
        return;
    if (!m_volumeAdjusted) {
        m_savedWaveVolume = saved;
        m_volumeAdjusted = true;
    } else {
        saved = m_savedWaveVolume; // 连续播放：始终基于最初保存值缩放
    }
    const double clamped = qBound(0.0, volume, 1.0);
    const WORD left = static_cast<WORD>(LOWORD(saved) * clamped);
    const WORD right = static_cast<WORD>(HIWORD(saved) * clamped);
    waveOutSetVolume(0, MAKELONG(left, right));

    const quint64 generation = ++m_volumeGeneration;
    QTimer::singleShot(5000, this, [this, generation] {
        if (generation == m_volumeGeneration && m_volumeAdjusted) {
            waveOutSetVolume(0, m_savedWaveVolume);
            m_volumeAdjusted = false;
        }
    });
#else
    Q_UNUSED(volume);
#endif
}
