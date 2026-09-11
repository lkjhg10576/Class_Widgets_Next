#pragma once

#include <QFont>
#include <QObject>
#include <QPoint>
#include <QVariant>

class AppPaths;
class ConfigStore;
class CWThemeManager;
class WidgetsModel;
class WidgetsWindow;
class TrayIcon;
class WidgetBackend;
class TranslatorStub;
class NotificationStub;
class ScheduleRuntimeStub;
class ScheduleEditorStub;
class ScheduleManagerStub;
class WindowManagerStub;
class ClassSwapManagerStub;
class UtilsBackendStub;
class PluginManagerStub;
class QQmlEngine;

// 对应上游 core/central.py 的 AppCentral（QML 名 "AppCentral"）。
// 上游 Python 版把全部服务聚在一个类里；本移植保留聚合门面（零改动迁移），
// 成员对象各自拆分实现，逐步在 M2-M4 替换 stub。
class AppCentral : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject *scheduleRuntime READ scheduleRuntime NOTIFY initialized)
    Q_PROPERTY(QObject *notification READ notification CONSTANT)
    Q_PROPERTY(QObject *scheduleEditor READ scheduleEditor NOTIFY initialized)
    Q_PROPERTY(QObject *classSwapManager READ classSwapManager NOTIFY initialized)
    Q_PROPERTY(QObject *scheduleManager READ scheduleManager NOTIFY updated)
    Q_PROPERTY(QObject *translator READ translator NOTIFY initialized)
    Q_PROPERTY(QObject *themeManager READ themeManager CONSTANT)
    Q_PROPERTY(bool restartRequired READ restartRequired NOTIFY restartRequiredChanged)
    Q_PROPERTY(QVariant globalConfig READ globalConfig NOTIFY initialized)

public:
    explicit AppCentral(QObject *parent = nullptr);
    ~AppCentral() override;

    // 装配全部服务并注册内置小组件（对应 _initialize_* + 插件注册的内置替身）
    void initialize();

    // 对应 central.py setup_qml_context：为每个引擎注册全部上下文属性，
    // 属性名与 Python 版逐字一致（137 个 QML 零改动的底线）。
    // 非 const：setContextProperty 需要 QObject*，const 方法里 this 无法转换
    void setupQmlContext(QQmlEngine *engine);

    // C++ 侧访问器
    WidgetsWindow *widgetsWindow() const { return m_widgetsWindow; }
    void setWidgetsWindow(WidgetsWindow *window) { m_widgetsWindow = window; }

    // --- QML 契约 ---
    // ⚠️ 返回 QObject* 的访问器必须在 AppCentral.cpp 中定义（类内只能前向声明
    // 这些 stub 类型，不完整类型无法向上转型）
    QObject *scheduleRuntime() const;
    QObject *notification() const;
    QObject *scheduleEditor() const;
    QObject *classSwapManager() const;
    QObject *scheduleManager() const;
    QObject *translator() const;
    QObject *themeManager() const;
    bool restartRequired() const { return m_restartRequired; }
    QVariant globalConfig() const;

    Q_INVOKABLE void quit();
    Q_INVOKABLE void restart();
    Q_INVOKABLE void markRestartRequired();
    Q_INVOKABLE void reportThemeLoadFailure(const QString &source);
    Q_INVOKABLE void openDebugger();
    Q_INVOKABLE void toggleWidgetsEditMode();
    // central.py:543 getQFont：带 fallback 的字体构造
    Q_INVOKABLE QFont getQFont(const QString &targetFont,
                               const QString &fallbackFont = QStringLiteral("Microsoft YaHei")) const;

    ConfigStore *configs() const { return m_configs; }
    WidgetsModel *widgetsModel() const { return m_widgetsModel; }
    CWThemeManager *themeManagerObject() const { return m_themeManager; }

public slots:
    // 托盘转发（tray.py togglePanel → central.togglePanel）
    void onTrayTogglePanel(const QPoint &pos);
    void onTrayEditModeRequested();

signals:
    void updated();
    void initialized();
    void togglePanel(const QPoint &pos);
    void widgetRegistered(const QString &widgetId);
    void retranslate();
    void trayShortcutRequested(const QString &shortcutId);
    void restartRequiredChanged(bool required);

private:
    void registerBuiltinWidgets();

    ConfigStore *m_configs = nullptr;
    CWThemeManager *m_themeManager = nullptr;
    WidgetsModel *m_widgetsModel = nullptr;
    WidgetsWindow *m_widgetsWindow = nullptr;
    TrayIcon *m_trayIcon = nullptr;
    WidgetBackend *m_widgetBackend = nullptr;

    // M1 占位（SupportStubs.h）
    TranslatorStub *m_translator = nullptr;
    NotificationStub *m_notification = nullptr;
    ScheduleRuntimeStub *m_scheduleRuntime = nullptr;
    ScheduleEditorStub *m_scheduleEditor = nullptr;
    ScheduleManagerStub *m_scheduleManager = nullptr;
    WindowManagerStub *m_windowManager = nullptr;
    ClassSwapManagerStub *m_classSwapManager = nullptr;
    UtilsBackendStub *m_utilsBackend = nullptr;
    PluginManagerStub *m_pluginManager = nullptr;

    bool m_restartRequired = false;
};
