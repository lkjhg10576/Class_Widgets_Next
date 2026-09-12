#include "AppCentral.h"

#include "AppPaths.h"
#include "BuiltinWidgets.h"
#include "CWThemeManager.h"
#include "ConfigStore.h"
#include "Logger.h"
#include "SupportStubs.h"
#include "TrayIcon.h"
#include "WidgetsModel.h"
#include "WidgetsWindow.h"
#include "notification/NotificationService.h"
#include "schedule/ClassSwapManager.h"
#include "schedule/ScheduleEditor.h"
#include "schedule/ScheduleManager.h"
#include "schedule/ScheduleRuntime.h"
#include "schedule/UnionTimer.h"
#include "themes/ThemeLoadErrorDialog.h"
#include "themes/ThemeRecovery.h"
#include "updater/UpdaterBridge.h"
#include "utils/Translator.h"
#include "utils/UtilsBackend.h"
#include "windows/AppWindowManager.h"
#include "automations/AutomationManager.h"

#include <QCoreApplication>
#include <QProcess>
#include <QQmlContext>
#include <QQmlEngine>
#include <QWindow>

AppCentral::AppCentral(QObject *parent)
    : QObject(parent)
{
}

AppCentral::~AppCentral() = default;

void AppCentral::initialize(bool enableFirstRunGate)
{
    // 对应 _initialize_cores：路径/配置/主题/小组件模型
    m_configs = new ConfigStore(AppPaths::instance().configsRoot(), this);
    m_themeManager = new CWThemeManager(&AppPaths::instance(), this);
    m_widgetsModel = new WidgetsModel(this);
    m_widgetsModel->setConfigStore(m_configs);

    // 对应 _initialize_utils（M3/M4 真实实现已就位）
    m_translator = new Translator(m_configs, this);
    m_notification = new NotificationService(m_configs, this);
    m_windowManager = new AppWindowManager(this, this);
    m_utilsBackend = new cwn::utils::UtilsBackend(m_configs, this);
    m_utilsBackend->setNotificationService(m_notification);
    m_utilsBackend->setWindowManager(m_windowManager);
    m_pluginManager = new PluginManagerStub(this);

    // 内置小组件注册表（替代 cw_widgets 插件，对应 _load_theme_and_plugins 的插件加载）
    registerBuiltinWidgets();

    // 加载配置与主题（对应 run() 里的 _load_config 与 _load_theme_and_plugins 的主题部分）
    m_configs->load();
    m_configs->startAutoSave();
    m_themeManager->setConfigStore(m_configs); // 启用配置锁检查/回写

    // M3 主题恢复链（对应 theme_recovery.py 与 windows.py 的 ThemeLoadErrorDialog）
    m_themeRecovery = new ThemeRecovery(m_themeManager, this);
    m_themeLoadErrorDialog = new ThemeLoadErrorDialog(this);

    const QVariantMap preferences =
        m_configs->data().toMap().value(QStringLiteral("preferences")).toMap();
    m_themeManager->load();
    m_themeManager->applyConfiguredTheme(
        preferences.value(QStringLiteral("current_theme")).toString());

    // 退出前的窗口资源释放（对应 central.py:318 清理步骤）——教程分支也要生效
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this] {
        if (m_windowManager)
            m_windowManager->releaseAll();
    });

    // 首次运行教程门（对应 central.py init() 229-236：tutorial_completed=false →
    // 只开教程窗口并中断后续初始化，教程完成后 QML 写键 + restart() 走正常流程）
    if (enableFirstRunGate) {
        bool tutorialCompleted = false;
        if (const auto done = m_configs->value(QStringLiteral("app.tutorial_completed")))
            tutorialCompleted = done->toBool(false);
        if (!tutorialCompleted) {
            m_waitingForTutorial = true;
            m_windowManager->openTutorial();
            cwn::Log::info(QStringLiteral("First run: waiting for tutorial completion"));
            emit initialized();
            return;
        }
    }

    // M2 课程表域（对应 central.py _load_schedule / _load_class_swap / _load_runtime）。
    // 放在 configs->load() 之后：各对象构造/初始化会读取配置键。
    m_scheduleManager = new ScheduleManager(m_configs, QString(), this);
    m_scheduleEditor = new ScheduleEditor(m_scheduleManager, this);
    m_classSwapManager = new ClassSwapManager(m_configs, m_scheduleManager, this);
    m_scheduleRuntime = new ScheduleRuntime(m_configs, m_scheduleManager, this);

    QString currentSchedule;
    if (const auto current = m_configs->value(QStringLiteral("schedule.current_schedule")))
        currentSchedule = current->toString();
    m_scheduleManager->load(currentSchedule.isEmpty()
                                ? QStringLiteral("New Schedule 1")
                                : currentSchedule);
    m_classSwapManager->loadSwapRecords();
    m_scheduleRuntime->refreshWith(m_scheduleManager->schedule());
    UnionTimer::instance().start(); // 对应 central.py:461 统一秒级刷新

    // M4 更新器与自动化（对应 central.py:464-467 _run_utils 的 updater/automation 部分）
    m_updaterBridge = new UpdaterBridge(m_configs, this);
    m_automationManager = new AutomationManager(m_configs, this);
    m_automationManager->setUpdaterBridge(m_updaterBridge);
    m_automationManager->initBuiltinTasks();
    // AutoHideTask 构造时读初值（上游直接读 runtime.current_status），先推送当前状态
    m_automationManager->onScheduleStatusChanged(m_scheduleRuntime->currentStatus());
    m_updaterBridge->maybeNotifyUpdateComplete(); // 对应 central.py:467-470

    connectServices();

    emit initialized();
    cwn::Log::info(QStringLiteral("AppCentral initialization completed"));
}

void AppCentral::connectServices()
{
    // ---- 通知 ↔ 课程表运行时（对应 runtime.py 的通知分发接线）----
    m_notification->setScheduleRuntimeSource(m_scheduleRuntime);
    connect(m_scheduleRuntime, &ScheduleRuntime::currentsChanged, m_notification,
            qOverload<const QString &>(&NotificationService::dispatchStatusChange));
    connect(m_scheduleRuntime, &ScheduleRuntime::updated, m_notification,
            &NotificationService::checkPreparationBell);

    // ---- 自动化任务驱动（对应 central.py:457 union_update_timer.tick → automation_manager.update）----
    connect(&UnionTimer::instance(), &UnionTimer::tick,
            m_automationManager, &AutomationManager::update);
    connect(m_scheduleRuntime, &ScheduleRuntime::currentsChanged,
            m_automationManager, &AutomationManager::onScheduleStatusChanged);

    // ---- 更新器 ----
    // 注意：restart 带默认参（const QString &reason = ...），PMF 直连会因
    // "槽参数多于信号参数" 在编译期被 static_assert 拒绝，必须经 lambda 转发
    connect(m_updaterBridge, &UpdaterBridge::restartRequested, this, [this] { restart(); });

    // ---- 主题恢复（对应 windows.py ThemeLoadErrorDialog 的请求链）----
    connect(m_themeRecovery, &ThemeRecovery::errorDialogRequested,
            m_themeLoadErrorDialog, &ThemeLoadErrorDialog::setErrorDetails);
    connect(m_themeLoadErrorDialog, &ThemeLoadErrorDialog::showRequested, this, [this] {
        if (m_windowManager) {
            m_windowManager->openThemeLoadError(m_themeLoadErrorDialog->failedThemeId(),
                                                m_themeLoadErrorDialog->recovered());
        }
    });

    // ---- 托盘快捷方式转发（对应 central.py:175-177）----
    connect(m_utilsBackend, &cwn::utils::UtilsBackend::trayShortcutRequested,
            this, &AppCentral::trayShortcutRequested);

    // ---- 翻译链（translator.py languageChanged → 全局 retranslate）----
    connect(m_translator, &Translator::languageChanged, this, &AppCentral::retranslate);
    connect(this, &AppCentral::retranslate,
            m_utilsBackend, &cwn::utils::UtilsBackend::retranslate);
    connect(this, &AppCentral::retranslate,
            m_notification, &NotificationService::retranslateProviders);
}

void AppCentral::setWidgetsWindow(WidgetsWindow *window)
{
    m_widgetsWindow = window;
    if (window && m_themeRecovery) {
        // 主窗口主题加载失败 → 恢复流程（theme_recovery.py）
        connect(window, &WidgetsWindow::themeLoadFailed,
                m_themeRecovery, &ThemeRecovery::handleFailure);
    }
}

void AppCentral::setTrayIcon(TrayIcon *icon)
{
    m_trayIcon = icon;
    if (!icon || !m_windowManager)
        return;

    // 托盘菜单 → 窗口管理（tray.py 菜单语义）
    connect(icon, &TrayIcon::openSettingsRequested,
            m_windowManager, &AppWindowManager::openSettings);
    connect(icon, &TrayIcon::openEditorRequested,
            m_windowManager, &AppWindowManager::openEditor);
    connect(icon, &TrayIcon::openClassSwapRequested,
            m_windowManager, &AppWindowManager::openClassSwap);
    connect(icon, &TrayIcon::openTutorialRequested,
            m_windowManager, &AppWindowManager::openTutorial);
    // 关于页是设置窗口的一页；托盘"关于"按上游语义打开设置（M3 简化，无页内跳转）
    connect(icon, &TrayIcon::openAboutRequested,
            m_windowManager, &AppWindowManager::openSettings);
    // 迷你模式切换（tray.py toggle_mini_mode：写 preferences.mini_mode）
    connect(icon, &TrayIcon::miniModeRequested, this, [this] {
        bool current = false;
        if (const auto v = m_configs->value(QStringLiteral("preferences.mini_mode")))
            current = v->toBool();
        m_configs->set(QStringLiteral("preferences.mini_mode"), !current);
    });
    connect(this, &AppCentral::retranslate, icon, &TrayIcon::retranslate);

    // 系统级通知出口 → 托盘气泡（tray.py:57-59 showMessage）
    connect(m_notification, &NotificationService::systemNotificationRequested,
            icon, &TrayIcon::showEditNotification);
    connect(m_updaterBridge, &UpdaterBridge::notificationRequested,
            icon, &TrayIcon::showEditNotification);
    connect(m_automationManager, &AutomationManager::taskNotification,
            icon, &TrayIcon::showEditNotification);
}

void AppCentral::registerBuiltinWidgets()
{
    static BuiltinWidgetProvider s_provider;
    if (!m_widgetBackend)
        m_widgetBackend = new WidgetBackend(this);

    const QList<WidgetDefinition> definitions = s_provider.widgets();
    for (WidgetDefinition definition : definitions) {
        definition.backendObj = m_widgetBackend;
        m_widgetsModel->addWidget(definition);
        emit widgetRegistered(definition.id);
    }
    cwn::Log::info(QStringLiteral("Registered %1 builtin widgets").arg(definitions.size()));
}

void AppCentral::setupQmlContext(QQmlEngine *engine)
{
    // 名字逐字对齐 central.py:401-418
    QQmlContext *context = engine->rootContext();
    context->setContextProperty(QStringLiteral("WidgetsModel"), m_widgetsModel);
    context->setContextProperty(QStringLiteral("Configs"), m_configs);
    context->setContextProperty(QStringLiteral("CWThemeManager"), m_themeManager);
    context->setContextProperty(QStringLiteral("PluginManager"), m_pluginManager);
    context->setContextProperty(QStringLiteral("AppCentral"), this);
    context->setContextProperty(QStringLiteral("WindowManager"), m_windowManager);
    context->setContextProperty(QStringLiteral("PathManager"), &AppPaths::instance());
    context->setContextProperty(QStringLiteral("ClassSwapManager"), m_classSwapManager);
    context->setContextProperty(QStringLiteral("UtilsBackend"), m_utilsBackend);
    context->setContextProperty(QStringLiteral("UpdaterBridge"), m_updaterBridge);
    context->setContextProperty(QStringLiteral("ThemeLoadErrorDialog"),
                                m_themeLoadErrorDialog);
    // 主题 URL 拦截器（对应 core.py:34 window.engine 挂拦截器）；
    // 顺序敏感：import path 在 RinUiWindowBase 里已按 src/qml 优先设置
    engine->addUrlInterceptor(m_themeManager->urlInterceptor());
}

QVariant AppCentral::globalConfig() const
{
    return m_configs ? m_configs->data() : QVariant();
}

// 返回 QObject* 的 Q_PROPERTY 访问器：课程表域与 stub 类型在此处是完整类型，可安全向上转型
QObject *AppCentral::scheduleRuntime() const { return m_scheduleRuntime; }
QObject *AppCentral::notification() const { return m_notification; }
QObject *AppCentral::scheduleEditor() const { return m_scheduleEditor; }
QObject *AppCentral::classSwapManager() const { return m_classSwapManager; }
QObject *AppCentral::scheduleManager() const { return m_scheduleManager; }
QObject *AppCentral::translator() const { return m_translator; }
QObject *AppCentral::themeManager() const { return m_themeManager; }

void AppCentral::quit()
{
    QCoreApplication::quit();
}

void AppCentral::restart(const QString &reason)
{
    // central.py restart 用 QProcess.startDetached 重启自身（可带 --update-done 等原因）；
    // M4 先落地"延迟 2 秒自启 + 退出"，与更新器安装脚本的时序兼容，M5 再串联安装包。
    cwn::Log::info(QStringLiteral("AppCentral.restart requested (reason=%1)")
                       .arg(reason.isEmpty() ? QStringLiteral("user") : reason));
    const QString appPath = QCoreApplication::applicationFilePath();
    const QStringList args = reason.isEmpty()
        ? QStringList{}
        : QStringList{ reason };
    QProcess::startDetached(appPath, args, QCoreApplication::applicationDirPath());
    QCoreApplication::quit();
}

void AppCentral::init()
{
    // 对应 central.py init()（启动编排）；本移植的启动编排已前移到 initialize()。
    // CheckSingleInstanceDialog.qml 的"继续"按钮调用本方法以保持 QML 契约。
    cwn::Log::info(QStringLiteral("AppCentral.init() called from QML (no-op in C++ port)"));
}

void AppCentral::markRestartRequired()
{
    if (m_restartRequired)
        return;
    m_restartRequired = true;
    emit restartRequiredChanged(true);
}

void AppCentral::reportThemeLoadFailure(const QString &source)
{
    // WidgetLoader.qml:35 的错误入口；交由 ThemeRecovery 处理（theme_recovery.py），
    // 需要弹窗时经 errorDialogRequested → ThemeLoadErrorDialog → 窗口层。
    cwn::Log::error(QStringLiteral("Theme component failed to load: %1").arg(source));
    if (m_themeRecovery)
        m_themeRecovery->reportComponentFailure(source);
}

void AppCentral::openDebugger()
{
    m_windowManager->openDebugger();
}

void AppCentral::toggleWidgetsEditMode()
{
    // 对应 central.py toggleWidgetsEditMode
    if (!m_widgetsWindow)
        return;
    QWindow *root = m_widgetsWindow->rootWindow();
    if (!root)
        return;

    QObject *widgetsLoader = root->findChild<QObject *>(QStringLiteral("widgetsLoader"));
    if (widgetsLoader) {
        root->raise();
        const bool current = widgetsLoader->property("editMode").toBool();
        widgetsLoader->setProperty("editMode", !current);
    }
}

QFont AppCentral::getQFont(const QString &targetFont, const QString &fallbackFont) const
{
    QStringList families;
    for (const QString &family : { targetFont, fallbackFont }) {
        const QString trimmed = family.trimmed();
        if (!trimmed.isEmpty())
            families.append(trimmed);
    }
    QFont font;
    font.setFamilies(families);
    font.setStyleHint(QFont::SansSerif);
    return font;
}

void AppCentral::onTrayTogglePanel(const QPoint &pos)
{
    emit togglePanel(pos);
}

void AppCentral::onTrayEditModeRequested()
{
    toggleWidgetsEditMode();
}
