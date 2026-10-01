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
#include "extensions/ExtensionManager.h"
#include "extensions/RollCallService.h"
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
#include "weather/WeatherService.h"
#include "windows/AppWindowManager.h"
#include "automations/AutomationManager.h"

#include <QCoreApplication>
#include <QProcess>
#include <QQmlContext>
#include <QQmlEngine>
#include <QTimer>
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

    // 天气小组件数据源（须先于 registerBuiltinWidgets 就绪，以便为其定义挂专属 backend）
    m_weatherService = new WeatherService(m_configs, m_notification, this);

    // 内置小组件注册表（替代 cw_widgets 插件，对应 _load_theme_and_plugins 的插件加载）
    registerBuiltinWidgets();

    // 加载配置与主题（对应 run() 里的 _load_config 与 _load_theme_and_plugins 的主题部分）
    m_configs->load();
    m_configs->startAutoSave();
    // 「扩展功能」框架（extensions-feature-plan 阶段 A）：静态注册表 + 开关状态。
    // 须先于天气迁移等后续接线就绪，因此放在配置加载之后、与天气服务相邻创建
    m_extensionManager = new ExtensionManager(m_configs, this);
    // C1（extensions-feature-plan §6）：随机点名服务（名单读写/加权抽取/txt 解析）。
    // 无网络无定时器，仅持 ConfigStore 指针，configs->load() 之后创建即可
    m_rollCallService = new RollCallService(m_configs, this);
    // B4（extensions-feature-plan §5）：天气 60s 轮询 tick 改由扩展开关控制——
    // 未启用时不唤醒网络检查。本行必须在 ExtensionManager 构造之后：构造内的
    // 一次性迁移（B3）若识别出存量天气实例会自动启用扩展，此处 isEnabled 即
    // 反映迁移结果，老用户升级后轮询行为无缝保持。首拉仍由 QML 组件 request() 触发。
    if (m_extensionManager->isEnabled(QStringLiteral("classwidgets.ext.weather")))
        m_weatherService->start();
    // B1：模型读取扩展开关过滤「添加小组件」列表中的天气定义（definitionsList）
    m_widgetsModel->setExtensionManager(m_extensionManager);
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

    // C5（extensions-feature-plan §6）：启动时点名扩展已启用 → 自动打开悬浮按钮窗。
    // 必须经 0ms 定时器延迟到事件循环：main() 里 WidgetsWindow::run()（创建共享
    // 主引擎）在本方法返回之后、app.exec() 之前同步执行；若在此立即 open，
    // AppWindowManager 会因主窗口缺席回退独立引擎（教程门场景即如此），白白多
    // 编译一整套 RinUI。定时器触发时共享引擎已就绪，走共享引擎路径。
    if (m_extensionManager->isEnabled(QStringLiteral("classwidgets.ext.rollCall"))) {
        QTimer::singleShot(0, this, [this] {
            if (m_windowManager)
                m_windowManager->openRollCallFloat();
        });
    }

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

    // ---- 「扩展功能」开关接线（§5 B2/B4 天气；§6 C5 随机点名）----
    // 开关翻转的三件联动：①「添加小组件」列表过滤（B1，definitionsList 依赖
    // 开关状态，须手动发 definitionChanged）；②实例自动增删（开启=当前预设
    // 无天气则补一个，关闭=全部预设清干净，城市已收敛全局键故移除无损失）；
    // ③轮询启停。启动期不经过这里：迁移不发 extensionToggled，已启用扩展的
    // 实例由 WidgetsWindow::run 的 loadConfig 从配置装载（推断已核实：loadPreset
    // 逐条读 presets，天气实例原样恢复）。
    connect(m_extensionManager, &ExtensionManager::extensionToggled, this,
            [this](const QString &id, bool enabled) {
                if (!m_widgetsModel) // 防御：模型缺席时仅跳过实例联动，不崩
                    return;
                m_widgetsModel->refreshDefinitions();
                if (id == QLatin1String("classwidgets.ext.weather")) {
                    if (enabled) {
                        if (m_weatherService)
                            m_weatherService->start();
                        if (m_widgetsModel->instanceCount(WeatherService::widgetTypeId()) == 0)
                            m_widgetsModel->addInstance(WeatherService::widgetTypeId());
                    } else {
                        if (m_weatherService)
                            m_weatherService->stop();
                        m_widgetsModel->removeAllInstancesOf(WeatherService::widgetTypeId());
                    }
                } else if (id == QLatin1String("classwidgets.ext.rollCall")) {
                    // C5（extensions-feature-plan §6）：开 → 弹悬浮点名按钮窗；
                    // 关 → 悬浮窗与结果窗一并下线（结果窗可能开着），并清会话
                    // 排除名单 —— 功能下线即整个会话作废，重新开启视为新会话
                    if (m_windowManager) {
                        if (enabled) {
                            m_windowManager->openRollCallFloat();
                        } else {
                            m_windowManager->closeRollCallFloat();
                            m_windowManager->closeRollCallResult();
                        }
                    }
                    if (!enabled && m_rollCallService)
                        m_rollCallService->clearSession();
                }
                // 课表速览（classwidgets.ext.schedulePeek）无需 C++ 接线：
                // SchedulePeekBar.qml 经 "Extensions" 上下文属性自行观测开关
                // （绑定内显式读 extensions 属性建立通知依赖），蒙版重算链由
                // WidgetsWindow::onQmlReady 直连速览条几何信号承担
            });
}

void AppCentral::setWidgetsWindow(WidgetsWindow *window)
{
    m_widgetsWindow = window;
    if (window && m_themeRecovery) {
        // 主窗口主题加载失败 → 恢复流程（theme_recovery.py）
        connect(window, &WidgetsWindow::themeLoadFailed,
                m_themeRecovery, &ThemeRecovery::handleFailure);
    }
    if (window && m_windowManager) {
        // B3 缓存纪律：辅助窗口销毁引擎后，安排主引擎低频 trim（脏合并）
        connect(m_windowManager, &AppWindowManager::auxiliaryWindowReleased,
                window, &WidgetsWindow::notifyAuxiliaryWindowReleased);
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
    // B4 托盘菜单扩展（用户反馈）：调休 / 切换课程表 / 重启；"关于"已砍（设置
    // 窗口首页即关于页，入口冗余）
    // 调休：复用内置快捷方式信号路径（与托盘面板宫格同 ID），QML 端 MainInterface 弹
    // RescheduleDayDialog
    connect(icon, &TrayIcon::rescheduleDayRequested, this, [this] {
        emit trayShortcutRequested(QStringLiteral("com.classwidgets.reschedule-day"));
    });
    // 切换课程表：MainInterface 弹 SwitchScheduleDialog（scheduleManager.load 完成切换）
    connect(icon, &TrayIcon::switchScheduleRequested,
            this, &AppCentral::traySwitchScheduleRequested);
    // 重启：AppCentral::restart 自启新实例后退出
    connect(icon, &TrayIcon::restartRequested, this, [this] { restart(); });
    // 退出：必须走本类 quit()（exit(0) 绕过 Qt 6.8+ quit() 的窗口关闭协商——
    // 悬浮小组件等 onClosing 拒绝关闭的窗口会吞掉 quit()，表现为"要点两次退出"）
    connect(icon, &TrayIcon::quitRequested, this, &AppCentral::quit);
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
        // 天气组件挂专属数据源 backend（WeatherService），其余组件共用通用 WidgetBackend；
        // 两类指针无继承关系，三元须先统一到 QObject* 才能推导公共类型（MSVC C2446）
        definition.backendObj = (definition.id == WeatherService::widgetTypeId())
                                    ? static_cast<QObject *>(m_weatherService)
                                    : static_cast<QObject *>(m_widgetBackend);
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
    // 「扩展功能」框架（本仓库新增上下文名，非 central.py 对齐项）
    context->setContextProperty(QStringLiteral("Extensions"), m_extensionManager);
    // 随机点名服务（阶段 C1）；"RollCall" 名字经 grep 确认与既有上下文无冲突
    context->setContextProperty(QStringLiteral("RollCall"), m_rollCallService);
    context->setContextProperty(QStringLiteral("UtilsBackend"), m_utilsBackend);
    context->setContextProperty(QStringLiteral("UpdaterBridge"), m_updaterBridge);
    context->setContextProperty(QStringLiteral("ThemeLoadErrorDialog"),
                                m_themeLoadErrorDialog);
    // A6：暴露全局秒级心跳，QML 侧（如 Time 挂件）订阅 tick 替代自开 QTimer
    // （与整秒对齐，消除 0.5s 偏移唤醒；见 UnionTimer.h 的"禁止再开秒级 QTimer"）
    context->setContextProperty(QStringLiteral("UnionTimer"), &UnionTimer::instance());
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
QObject *AppCentral::weather() const { return m_weatherService; }
QObject *AppCentral::rollCall() const { return m_rollCallService; }
QObject *AppCentral::scheduleManager() const { return m_scheduleManager; }
QObject *AppCentral::translator() const { return m_translator; }
QObject *AppCentral::themeManager() const { return m_themeManager; }

void AppCentral::quit()
{
    // Qt 6.8+ 的 quit() 会先向所有可见窗口发送关闭请求，任一窗口在 onClosing 里
    // 拒绝关闭（教程/设置等受管窗口均 accepted=false）就会放弃整个退出请求。
    // exit() 无条件停止事件循环，不经过窗口关闭协商。
    QCoreApplication::exit(0);
}

void AppCentral::relaunchIfNeeded()
{
    if (!m_relaunchRequested)
        return;
    m_relaunchRequested = false;
    QProcess::startDetached(QCoreApplication::applicationFilePath(), m_relaunchArgs,
                            QCoreApplication::applicationDirPath());
}

void AppCentral::restart(const QString &reason)
{
    // central.py restart 用 QProcess.startDetached 重启自身（可带 --update-done 等原因）。
    // 注意两点（完成引导卡死问题的修复）：
    // 1. 用 exit() 而非 quit()：quit() 会请求窗口关闭，教程窗口 onClosing 拒绝后
    //    退出被放弃，转而弹出"关闭引导"确认框且反复出现；
    // 2. 这里只登记重启意图，新实例由 main() 在单实例锁释放后经 relaunchIfNeeded()
    //    拉起 —— 否则新进程抢不到锁（QGuiApplication 尚未退出）自退，应用直接消失。
    cwn::Log::info(QStringLiteral("AppCentral.restart requested (reason=%1)")
                       .arg(reason.isEmpty() ? QStringLiteral("user") : reason));
    m_relaunchRequested = true;
    m_relaunchArgs = reason.isEmpty()
        ? QStringList{}
        : QStringList{ reason };
    QCoreApplication::exit(0);
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

void AppCentral::onTrayEditModeRequested()
{
    toggleWidgetsEditMode();
}
