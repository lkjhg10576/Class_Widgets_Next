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

#include <QQmlContext>
#include <QQmlEngine>

AppCentral::AppCentral(QObject *parent)
    : QObject(parent)
{
}

AppCentral::~AppCentral() = default;

void AppCentral::initialize()
{
    // 对应 _initialize_cores：路径/配置/主题/小组件模型
    m_configs = new ConfigStore(AppPaths::instance().configsRoot(), this);
    m_themeManager = new CWThemeManager(&AppPaths::instance(), this);
    m_widgetsModel = new WidgetsModel(this);
    m_widgetsModel->setConfigStore(m_configs);

    // 对应 _initialize_utils（stub 部分，M2-M4 逐个替换）
    m_translator = new TranslatorStub(m_configs, this);
    m_notification = new NotificationStub(this);
    m_scheduleManager = new ScheduleManagerStub(m_configs, this);
    m_scheduleRuntime = new ScheduleRuntimeStub(this);
    m_windowManager = new WindowManagerStub(this);
    m_classSwapManager = new ClassSwapManagerStub(this);
    m_utilsBackend = new UtilsBackendStub(this);
    m_pluginManager = new PluginManagerStub(this);

    // 内置小组件注册表（替代 cw_widgets 插件，对应 _load_theme_and_plugins 的插件加载）
    registerBuiltinWidgets();

    // 加载配置与主题（对应 run() 里的 _load_config 与 _load_theme_and_plugins 的主题部分）
    m_configs->load();
    m_configs->startAutoSave();

    const QVariantMap preferences =
        m_configs->data().toMap().value(QStringLiteral("preferences")).toMap();
    m_themeManager->load();
    m_themeManager->applyConfiguredTheme(
        preferences.value(QStringLiteral("current_theme")).toString());

    emit initialized();
    cwn::Log::info(QStringLiteral("AppCentral initialization completed"));
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

void AppCentral::setupQmlContext(QQmlEngine *engine) const
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
}

QVariant AppCentral::globalConfig() const
{
    return m_configs ? m_configs->data() : QVariant();
}

void AppCentral::quit()
{
    QCoreApplication::quit();
}

void AppCentral::restart()
{
    // central.py restart 用 QProcess.startDetached 重启自身；
    // M1 先退出去，M5 与安装包/更新器串联时落地。
    cwn::Log::warn(QStringLiteral("AppCentral.restart: full restart lands in M5, quitting now"));
    QCoreApplication::quit();
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
    // WidgetLoader.qml:35 的错误入口；上游经 ThemeRecoveryController 弹恢复窗（M3）。
    cwn::Log::error(QStringLiteral("Theme component failed to load: %1").arg(source));
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
