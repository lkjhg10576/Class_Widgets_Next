#pragma once

#include <QHash>
#include <QJsonValue>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include "NotificationModel.h"

class ConfigStore;
class NotificationProvider;

// 对应上游 src/core/notification/manager.py（NotificationManager，QML 名
// "AppCentral.notification"）与 service.py（NotificationService，上游经
// UtilsBackend 委托给 QML 设置页）。上游拆成两个类，C++ 侧合并为一个
// NotificationService：AppCentral::notification 直接暴露它，未来的
// UtilsBackend 实现将通知设置方法逐字委托到这里（见 utils/backend.py:311-448
// 的委托面）。
//
// QML 消费契约（已 grep 全量核对，方法/信号名逐字一致）：
//   FloatingWidget.qml:295 / dynamicNotification.qml:48
//       → notifyQmlReady()
//   FloatingWidget.qml:51-65 / dynamicNotification.qml:189-209
//   Debugger/EditSchedule.qml:31-40
//       → signal notified(payload)，payload 形状见 NotificationModel.h
//   设置页 Notification.qml 经 UtilsBackend.* 间接触达本类方法
//       （getGlobalLevelSound / playNotificationSoundLevel /
//        selectNotificationSound / notificationProviders /
//        setNotificationProviderEnabled / setNotificationProviderSystemNotify /
//        setNotificationProviderAppNotify / getGlobalVolume / setGlobalVolume /
//        getNotificationsEnabled / setNotificationsEnabled）
//
// 与上游差异（保守移植）：
//   1. manager.py:29 的 threading.Lock 省略 —— 全部调用都在 GUI 线程
//      （QML 插槽 + 信号投递），无跨线程竞争；
//   2. service.py 的 QSoundEffect 音效改用 Win32 PlaySoundW
//      （SND_FILENAME | SND_ASYNC，不阻塞主线程；音量经 waveOutSetVolume
//       尽力逼近，见 NotificationService.cpp 注释）；
//   3. manager.py:118-123 的 tray_icon.push_notification 改为发射
//      systemNotificationRequested 信号，由主控连接到 TrayIcon。
class NotificationService : public QObject
{
    Q_OBJECT
    // manager.py:146-165 get_providers + utils/backend.py:311-316（QML 设置页
    // 以属性形式读取：Notification.qml:258 model: UtilsBackend.notificationProviders）
    Q_PROPERTY(QVariantList notificationProviders READ notificationProviders
                   NOTIFY notificationProvidersChanged)

public:
    explicit NotificationService(ConfigStore *configs, QObject *parent = nullptr);
    ~NotificationService() override;

    // provider.py:39-41 的自动获取兜底：最近创建的实例（AppCentral 创建服务时
    // 自动成为 default；仅用于 Provider 构造未显式传 service 的场景）
    static NotificationService *defaultInstance() { return s_defaultInstance; }

    // --- manager.py:31-43 provider 注册 ---
    void registerProvider(NotificationProvider *provider);
    void unregisterProvider(const QString &providerId);
    // manager.py:45-47 is_enabled
    bool isProviderEnabled(const QString &providerId) const;

    // 读取 provider 配置段（provider.py:49-56 get_config 的实现侧）；
    // 返回 {enabled, use_system_notify, use_app_notify}，键名照 model.py:40-43。
    QVariantMap providerConfig(const QString &providerId) const;

    // --- manager.py:88-142 dispatch：通知分发主入口 ---
    // 全局开关 / provider 开关检查 → 系统通知信号 → QML 信号（QML 未就绪时排队）
    // → 音效。级别/音效语义见 NotificationModel.h。
    void dispatch(const cwn::notification::NotificationData &data);

    // --- runtime.py:302-443 _update_notify 的状态变化分发表 ---
    // status 取 ScheduleRuntime::currentStatus（"class"/"activity"/"preparation"/
    // "break"/"free"，见 ScheduleModel.h:33-37）；entry/subject/nextEntries/
    // subjects 传 ScheduleRuntime 同名属性的值。内部按"日期+状态+条目指纹"去重
    // （对应上游 previous_entry != current_entry 的判定，runtime.py:303），因此
    // 同时连接 currentsChanged 与 updated() 也不会重复发送。
    Q_INVOKABLE void dispatchStatusChange(const QString &status,
                                          const QVariantMap &currentEntry,
                                          const QVariantMap &currentSubject,
                                          const QVariantList &nextEntries,
                                          const QVariantList &subjects);
    // 便捷重载：从注入的运行时对象动态读取上述属性（避免 notification/ 依赖
    // schedule/ 头文件；运行时对象仅需暴露 currentStatus/currentEntry/
    // currentSubject/nextEntries/subjects 属性——ScheduleRuntime 均满足）。
    Q_INVOKABLE void dispatchStatusChange(const QString &status);

    // --- runtime.py:445-496 预备铃 ---
    // 上游在 _update_notify 中按"当前时间 == 下一节开始时间 - preparation_time
    // 分钟"的整秒相等判定触发；C++ 侧 ScheduleRuntime 暂无该钩子，由主控在
    // ScheduleRuntime::updated() 里调用本方法（内部含去重，重复调用安全）。
    // 时间基准 = QDateTime::currentDateTime() + configs schedule.time_offset
    // （对应 runtime.py:263-264）。
    Q_INVOKABLE void checkPreparationBell();

    // 设置动态属性读取源（schedule 运行时）；未设置时便捷重载与
    // checkPreparationBell 为空操作。
    void setScheduleRuntimeSource(QObject *runtimeSource);

    // --- QML 契约 ---
    // manager.py:50-56：QML 加载完成后调用（FloatingWidget.qml:295 /
    // dynamicNotification.qml:48），自动补发排队中的通知。
    Q_INVOKABLE void notifyQmlReady();
    bool qmlReady() const { return m_qmlReady; } // C++ 侧查询用

    // --- 设置页服务面（service.py 全量 + backend.py 委托面）---
    QVariantList notificationProviders() const; // manager.py:146-165 get_providers

    // service.py:35-60 三个 provider 开关
    Q_INVOKABLE void setNotificationProviderEnabled(const QString &providerId, bool enabled);
    Q_INVOKABLE void setNotificationProviderSystemNotify(const QString &providerId, bool useSystem);
    Q_INVOKABLE void setNotificationProviderAppNotify(const QString &providerId, bool useApp);

    // service.py:63-77 级别音效
    Q_INVOKABLE void setLevelSound(int level, const QString &sound);
    Q_INVOKABLE QString getLevelSound(int level) const;

    // service.py:79-97 全局开关与音量
    Q_INVOKABLE double getNotificationVolume() const;
    Q_INVOKABLE void setNotificationVolume(double volume);
    Q_INVOKABLE void setNotificationsEnabled(bool enabled);
    Q_INVOKABLE bool getNotificationsEnabled() const;

    // service.py:100-118 provider 级别音效（上游实现即忽略 provider_id）
    Q_INVOKABLE QString getNotificationProviderLevelSound(const QString &providerId, int level) const;
    Q_INVOKABLE void setNotificationProviderLevelSound(const QString &providerId, int level, const QString &sound);

    // service.py:110-128 全局级别音效与音量
    Q_INVOKABLE QString getGlobalLevelSound(int level) const;
    Q_INVOKABLE void setGlobalLevelSound(int level, const QString &sound);
    Q_INVOKABLE double getGlobalVolume() const;
    Q_INVOKABLE void setGlobalVolume(double volume);

    // service.py:130-138 全局音量别名
    Q_INVOKABLE double getGlobalNotificationVolume() const;
    Q_INVOKABLE void setGlobalNotificationVolume(double volume);

    // service.py:141-203 音效播放
    Q_INVOKABLE void playNotificationSoundLevel(int level);
    Q_INVOKABLE void playNotificationSound(const QString &providerId, int level);

    // service.py:205-235 选择音效文件（复制到 assets/audio 后以相对路径保存）
    Q_INVOKABLE bool selectNotificationSound(int level);

    // runtime.py:118-135 retranslate：原地刷新各 provider 显示名
    // （上游是注销重建；C++ 侧改名 + notificationProvidersChanged 等效）。
    // 主控连接 AppCentral::retranslate → 本槽。
    void retranslateProviders();

signals:
    // manager.py:20 notified = Signal(dict) —— QML 契约信号，payload 见
    // NotificationModel.h（title/message/icon/level/duration + 形状键）
    void notified(const QVariantMap &payload);
    void notificationProvidersChanged();
    // manager.py:116-125 的系统通知路径（上游调 tray_icon.push_notification）。
    // 主控连接到 TrayIcon（现有 showEditNotification(title, text) 即
    // QSystemTrayIcon::showMessage，5s，可直接对接）。
    void systemNotificationRequested(const QString &title, const QString &message);

private:
    // 配置读写辅助（notifications.* 点分路径，值默认与 config/model.py:249-266 对齐）
    bool notificationsEnabled() const;
    double notificationVolume() const;
    QString levelSound(int level) const; // notifications.level_sounds.<level>
    void setProviderConfigField(const QString &providerId, const QString &field,
                                const QJsonValue &value);
    void emitProvidersChanged();
    // runtime.py:338-405 的 "Next: ..." / "Coming up: ..." 消息构造（两处分支共用）
    QString nextEntryMessage(const QVariantMap &nextEntry, const QVariantList &subjects,
                             bool comingUp) const;
    // service.py:192-200 QSoundEffect::setVolume 的语义映射（PlaySound 无音量参数）
    void applyPlaybackVolume(double volume);

    static NotificationService *s_defaultInstance;

    ConfigStore *m_configs = nullptr;
    QHash<QString, NotificationProvider *> m_providers; // manager.py:24
    QStringList m_providerOrder;                        // 注册顺序（QHash 无序，设置页列表需稳定）
    bool m_qmlReady = false;                            // manager.py:27
    QVector<QVariantMap> m_pendingNotifications;        // manager.py:28（QML 未就绪的排队）
    QObject *m_runtimeSource = nullptr;                 // 便捷重载的动态属性源

    QString m_lastStatusKey;  // dispatchStatusChange 去重（对应 previous_entry 判定）
    QString m_lastBellKey;    // 预备铃去重（同一目标分钟只发一次）
    bool m_volumeAdjusted = false; // 播放音量的保存/恢复状态（waveOutSetVolume）
    quint32 m_savedWaveVolume = 0;
    quint64 m_volumeGeneration = 0;
};
