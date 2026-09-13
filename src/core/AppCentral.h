#pragma once

#include <QFont>
#include <QObject>
#include <QPoint>
#include <QStringList>
#include <QVariant>

class AppPaths;
class AppWindowManager;
class AutomationManager;
class ClassSwapManager;
class ConfigStore;
class CWThemeManager;
class ScheduleEditor;
class ScheduleManager;
class ScheduleRuntime;
class ThemeLoadErrorDialog;
class ThemeRecovery;
class Translator;
class NotificationService;
class UpdaterBridge;
class WidgetsModel;
class WidgetsWindow;
class TrayIcon;
class WeatherService;
class WidgetBackend;
namespace cwn { namespace utils { class UtilsBackend; } }
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
    Q_PROPERTY(QObject *weather READ weather CONSTANT)
    Q_PROPERTY(QObject *scheduleManager READ scheduleManager NOTIFY updated)
    Q_PROPERTY(QObject *translator READ translator NOTIFY initialized)
    Q_PROPERTY(QObject *themeManager READ themeManager CONSTANT)
    Q_PROPERTY(bool restartRequired READ restartRequired NOTIFY restartRequiredChanged)
    Q_PROPERTY(QVariant globalConfig READ globalConfig NOTIFY initialized)

public:
    explicit AppCentral(QObject *parent = nullptr);
    ~AppCentral() override;

    // 装配全部服务并注册内置小组件（对应 _initialize_* + 插件注册的内置替身）。
    // enableFirstRunGate=false 时跳过首跑教程门（CI 冒烟测试用）
    void initialize(bool enableFirstRunGate = true);

    // 首次运行教程门是否触发（对应 central.py init() 的 WAITING_FOR_TUTORIAL 分支）；
    // 为 true 时 main.cpp 不创建主窗口/托盘，事件循环仅承载教程窗口
    bool isWaitingForTutorial() const { return m_waitingForTutorial; }

    // restart() 只登记重启意图并退出事件循环；main.cpp 在单实例锁释放后调用本方法
    // 拉起新实例（若在 restart() 里 startDetached，新进程会因锁未释放而自退，
    // 表现为"重启/完成引导后应用消失"）
    bool isRelaunchRequested() const { return m_relaunchRequested; }
    void relaunchIfNeeded();

    // 对应 central.py setup_qml_context：为每个引擎注册全部上下文属性，
    // 属性名与 Python 版逐字一致（137 个 QML 零改动的底线）。
    // 非 const：setContextProperty 需要 QObject*，const 方法里 this 无法转换
    void setupQmlContext(QQmlEngine *engine);

    // C++ 侧访问器
    WidgetsWindow *widgetsWindow() const { return m_widgetsWindow; }
    void setWidgetsWindow(WidgetsWindow *window);
    // main.cpp 创建托盘后注入；内部完成通知/托盘菜单的全部信号接线
    void setTrayIcon(TrayIcon *icon);
    AppWindowManager *windowManager() const { return m_windowManager; }

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
    QObject *weather() const;
    bool restartRequired() const { return m_restartRequired; }
    QVariant globalConfig() const;

    Q_INVOKABLE void quit();
    // Tutorial.qml 调 restart("--update-done")：带可选原因的重启（更新完成后走同一路径）
    Q_INVOKABLE void restart(const QString &reason = QString());
    Q_INVOKABLE void markRestartRequired();
    // CheckSingleInstanceDialog.qml 调 AppCentral.init()：单实例对话框的"继续"按钮入口
    // （上游 central.py init() 做启动编排；本移植启动编排已前移到 initialize()，此处兜底）
    Q_INVOKABLE void init();
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
    // B4 托盘菜单："切换课程表"（QML 端 MainInterface 弹 SwitchScheduleDialog）
    void traySwitchScheduleRequested();
    void restartRequiredChanged(bool required);

private:
    void registerBuiltinWidgets();
    // M2-M4 各域对象间的信号接线（initialize() 末尾统一执行）
    void connectServices();

    ConfigStore *m_configs = nullptr;
    CWThemeManager *m_themeManager = nullptr;
    ThemeRecovery *m_themeRecovery = nullptr;
    ThemeLoadErrorDialog *m_themeLoadErrorDialog = nullptr;
    WidgetsModel *m_widgetsModel = nullptr;
    WidgetsWindow *m_widgetsWindow = nullptr;
    TrayIcon *m_trayIcon = nullptr;
    WidgetBackend *m_widgetBackend = nullptr;
    WeatherService *m_weatherService = nullptr;

    // M1 占位已全部替换（M2-M4）：SupportStubs 仅剩 PluginManagerStub
    Translator *m_translator = nullptr;
    NotificationService *m_notification = nullptr;
    ScheduleRuntime *m_scheduleRuntime = nullptr;
    ScheduleEditor *m_scheduleEditor = nullptr;
    ScheduleManager *m_scheduleManager = nullptr;
    AppWindowManager *m_windowManager = nullptr;
    ClassSwapManager *m_classSwapManager = nullptr;
    cwn::utils::UtilsBackend *m_utilsBackend = nullptr;
    PluginManagerStub *m_pluginManager = nullptr;
    UpdaterBridge *m_updaterBridge = nullptr;
    AutomationManager *m_automationManager = nullptr;

    bool m_restartRequired = false;
    bool m_waitingForTutorial = false;
    bool m_relaunchRequested = false;
    QStringList m_relaunchArgs;
};
