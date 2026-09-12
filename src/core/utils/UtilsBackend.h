#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QVector>

class ConfigStore;
class NotificationService;

namespace cwn {
namespace utils {

class LogListModel;
class LogFilterProxyModel;

// 对应上游 src/core/utils/backend.py UtilsBackend（QML 名 "UtilsBackend"，
// app/src/qml 13 个文件引用）。上游挂接插件 API（plugin_api.ui）的快捷方式/
// 设置页注册与 loguru 日志钩子；本移植按以下方式对齐：
//   1. 快捷方式：内置注册表逐字复刻 central.py:142-170 的 5 个 builtin 项
//      （id/name/icon/iconIsSource/owner 形状同 plugin/components.py:420-425），
//      插件注册留待 Phase 2；
//   2. 日志：链式接管 Qt 消息处理器（qInstallMessageHandler），先回放
//      Logger 已装处理器（stderr + 日志文件），再把条目经队列信号 marshal
//      回 GUI 线程喂给 LogListModel（对应 backend.py:27/80-101 的信号转发）；
//   3. 通知设置方法（backend.py:311-463）逐字委托 NotificationService 同名
//      方法（构造后经 setNotificationService 注入）；
//   4. 插件页（extraSettings）与调试通知 Provider 返回空/null 并注释原因，
//      消费 QML 均有空态兜底。
//
// 集成（AppCentral::initialize 替换 UtilsBackendStub 时）：
//   m_utilsBackend = new UtilsBackend(m_configs, this);
//   m_utilsBackend->setNotificationService(m_notification);
//   m_utilsBackend->setWindowManager(m_windowManager);
class UtilsBackend : public QObject
{
    Q_OBJECT
    // backend.py:142-144 @Property(list, notify=extraSettingsChanged)
    Q_PROPERTY(QVariantList extraSettings READ extraSettings NOTIFY extraSettingsChanged)
    // backend.py:146-163 三个快捷方式列表（形状 {id,name,icon,iconIsSource,owner}）
    Q_PROPERTY(QVariantList shortcuts READ shortcuts NOTIFY shortcutsChanged)
    Q_PROPERTY(QVariantList availableShortcuts READ availableShortcuts NOTIFY shortcutsChanged)
    Q_PROPERTY(QVariantList allShortcuts READ allShortcuts NOTIFY shortcutsChanged)
    // backend.py:216-218 @Property(str, notify=licenseLoaded)
    Q_PROPERTY(QString licenseText READ licenseText NOTIFY licenseLoaded)
    // backend.py:311-316 @Property(list, notify=notificationProvidersChanged)
    Q_PROPERTY(QVariantList notificationProviders READ notificationProviders
                   NOTIFY notificationProvidersChanged)
    // backend.py:102-107 @Property(QObject, constant=True) —— LogFilterProxyModel
    Q_PROPERTY(QObject *logs READ logs CONSTANT)
    // backend.py:220-225 调试通知 Provider（C++ 侧暂返回 nullptr，见实现注释）
    Q_PROPERTY(QObject *debugNotificationProvider READ debugNotificationProvider CONSTANT)

public:
    explicit UtilsBackend(ConfigStore *configs, QObject *parent = nullptr);
    ~UtilsBackend() override;

    // backend.py:32 self.notification_service = app.notification_service
    void setNotificationService(NotificationService *service);
    // backend.py:167 executeShortcut 的动作载体：需要暴露 Q_INVOKABLE
    // openSettings / openEditor / openPluginPlaza / openClassSwap 的窗口管理器
    //（M1 为 WindowManagerStub，M3 换真实实现后方法名不变——QML 契约）
    void setWindowManager(QObject *manager);

    // --- 属性读取 ---
    QObject *logs() const;
    QObject *debugNotificationProvider() const;
    QVariantList extraSettings() const;
    QVariantList shortcuts() const;
    QVariantList availableShortcuts() const;
    QVariantList allShortcuts() const;
    QString licenseText() const;
    QVariantList notificationProviders() const;

public slots:
    // central.py:179-185 _retranslate_builtin_shortcuts：语言切换后刷新快捷方式
    // 名称（名称翻译在取值时进行，重发 shortcutsChanged 即完成 retranslate）
    void retranslate();

    // --- Debugger 日志面板（backend.py:109-139）---
    Q_INVOKABLE void setLogFilterText(const QString &text);
    Q_INVOKABLE void setLogFilterLevel(const QString &level);
    // 返回 [成功(bool), 清理量(KB, 两位小数)] —— About.qml:195 按 resultTuple[0/1] 消费
    Q_INVOKABLE QVariantList clearLogs();

    // --- 快捷方式（backend.py:165-213）---
    Q_INVOKABLE bool executeShortcut(const QString &shortcutId);
    Q_INVOKABLE bool setShortcutEnabled(const QString &shortcutId, bool enabled);
    Q_INVOKABLE bool moveShortcutTo(const QString &shortcutId, int targetIndex);

    // --- 剪贴板 / 自启动 / 桌面快捷方式（backend.py:241-307）---
    Q_INVOKABLE bool copyToClipboard(const QString &text);
    // backend.py:252-254 autostartSupported 为 @Property；QML 存在两种调用式
    // （General/Index.qml:185 带 ()，tutorial/Preferences.qml:33 不带），故用
    // Q_INVOKABLE 方法：带 () 正常取值，裸引用得函数对象恒真（Windows 恒支持，
    // 语义一致），避免属性被误调用抛 TypeError
    Q_INVOKABLE bool autostartSupported();
    Q_INVOKABLE bool setAutostart(bool enabled);
    Q_INVOKABLE bool autostartEnabled();
    Q_INVOKABLE bool createDesktopShortcut();

    // --- 通知设置委托（backend.py:311-463 → NotificationService 同名方法）---
    Q_INVOKABLE void setNotificationProviderEnabled(const QString &providerId, bool enabled);
    Q_INVOKABLE void setNotificationProviderSystemNotify(const QString &providerId, bool useSystem);
    Q_INVOKABLE void setNotificationProviderAppNotify(const QString &providerId, bool useApp);
    Q_INVOKABLE void setLevelSound(int level, const QString &sound);
    Q_INVOKABLE QString getLevelSound(int level) const;
    // backend.py:353-365 与 427-440 重复定义了 setGlobalVolume/getGlobalVolume，
    // Python 后者覆盖前者（委托 service.getGlobalVolume）；C++ 只保留一份
    Q_INVOKABLE void setGlobalVolume(double volume);
    Q_INVOKABLE double getGlobalVolume() const;
    Q_INVOKABLE bool getNotificationsEnabled() const;
    Q_INVOKABLE void setNotificationsEnabled(bool enabled);
    // backend.py:383-395 provider 版音量读写（实现忽略 provider_id）
    Q_INVOKABLE double getNotificationProviderVolume(const QString &providerId) const;
    Q_INVOKABLE void setNotificationProviderVolume(const QString &providerId, double volume);
    // backend.py:398-410 provider 版级别音效（实现忽略 provider_id）
    Q_INVOKABLE QString getNotificationProviderLevelSound(const QString &providerId,
                                                          int level) const;
    Q_INVOKABLE void setNotificationProviderLevelSound(const QString &providerId, int level,
                                                       const QString &sound);
    Q_INVOKABLE QString getGlobalLevelSound(int level) const;
    Q_INVOKABLE void setGlobalLevelSound(int level, const QString &sound);
    Q_INVOKABLE void playNotificationSoundLevel(int level);
    Q_INVOKABLE void playNotificationSound(const QString &providerId, int level);
    // 上游返回 str（backend.py:458-463），C++ NotificationService 约定为 bool
    //（选择是否成功）；QML 未消费返回值，安全对齐
    Q_INVOKABLE bool selectNotificationSound(int level);

signals:
    // backend.py:18-21
    void extraSettingsChanged();
    void licenseLoaded();
    void notificationProvidersChanged();
    void shortcutsChanged();
    // central.py:82/175-177 trayShortcutRequested：内置"Reschedule Day"快捷方式
    // 不直接执行动作，改为转发（上游由 central 发出；集成时与本信号连接：
    // connect(utilsBackend, &UtilsBackend::trayShortcutRequested,
    //         appCentral, &AppCentral::trayShortcutRequested)）
    void trayShortcutRequested(const QString &shortcutId);
    // backend.py:27 _log_appended —— 日志条目 marshal 信号（消息处理器线程发出，
    // 经 Qt::QueuedConnection 落到 GUI 线程的 LogListModel::appendEntry）
    void logArrived(const QVariantMap &entry);

private:
    // plugin/components.py:420-425 ShortcutPayload 形状
    struct ShortcutEntry
    {
        QString id;
        const char *nameSource = nullptr; // "Shortcuts" 上下文的英文源文
        QString icon;                     // 字体图标名或图片 URI
        bool iconIsSource = false;        // true = icon 为 URI（components.py:389-400）
        QString owner = QStringLiteral("builtin");
    };

    void registerBuiltinShortcuts(); // central.py:142-170
    void loadLicense();              // backend.py:227-239
    void attachLogCapture();         // backend.py:80-95 的 C++ 等价物
    static void messageDispatch(QtMsgType type, const QMessageLogContext &context,
                                const QString &message);

    QStringList configuredShortcuts() const;              // preferences.shortcuts
    bool writeConfiguredShortcuts(const QStringList &ids); // backend.py:184/212
    QVariantMap shortcutPayload(const ShortcutEntry &entry) const;
    bool invokeWindowManager(const char *method); // openSettings 等 Q_INVOKABLE 转发

    ConfigStore *m_configs = nullptr;
    QPointer<NotificationService> m_notifications;
    QPointer<QObject> m_windowManager;
    LogListModel *m_logModel = nullptr;
    LogFilterProxyModel *m_logProxy = nullptr;
    QString m_licenseText;
    QVector<ShortcutEntry> m_registry; // 插入序即展示序（同上游 dict 语义）

    static UtilsBackend *s_instance;          // 消息处理器回调定位（AppCentral 单例装配）
    static QtMessageHandler s_previousHandler; // 链回 Logger 的处理器
};

} // namespace utils
} // namespace cwn
