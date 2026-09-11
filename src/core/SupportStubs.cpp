#include "SupportStubs.h"

#include "ConfigStore.h"
#include "Logger.h"

#include <QLocale>

TranslatorStub::TranslatorStub(const ConfigStore *configs, QObject *parent)
    : QObject(parent)
    , m_configs(configs)
{
}

QString TranslatorStub::language() const
{
    if (!m_configs)
        return QStringLiteral("en_US");
    return m_configs->data().toMap()
        .value(QStringLiteral("locale"))
        .toMap()
        .value(QStringLiteral("language"))
        .toString();
}

void TranslatorStub::setLanguage(const QString &language)
{
    cwn::Log::info(QStringLiteral("TranslatorStub: language switch to '%1' is not applied "
                                  "until M4 (translations) lands")
                       .arg(language));
    emit languageChanged(language);
}

QString TranslatorStub::tr(const QString &context, const QString &sourceText) const
{
    // M4 前直接回显源文本
    Q_UNUSED(context);
    return sourceText;
}

QString TranslatorStub::getSystemLanguage() const
{
    return QLocale::system().name();
}

NotificationStub::NotificationStub(QObject *parent)
    : QObject(parent)
{
}

ScheduleRuntimeStub::ScheduleRuntimeStub(QObject *parent)
    : QObject(parent)
{
}

ScheduleManagerStub::ScheduleManagerStub(const ConfigStore *configs, QObject *parent)
    : QObject(parent)
    , m_configs(configs)
{
}

QString ScheduleManagerStub::currentScheduleName() const
{
    if (!m_configs)
        return QStringLiteral("New Schedule 1");
    return m_configs->data().toMap()
        .value(QStringLiteral("schedule"))
        .toMap()
        .value(QStringLiteral("current_schedule"))
        .toString();
}

void ScheduleManagerStub::load(const QString &name)
{
    cwn::Log::info(QStringLiteral("ScheduleManagerStub: load '%1' is deferred to M2").arg(name));
}

WindowManagerStub::WindowManagerStub(QObject *parent)
    : QObject(parent)
{
}

ScheduleEditorStub::ScheduleEditorStub(QObject *parent)
    : QObject(parent)
{
}

void WindowManagerStub::openSettings()
{
    cwn::Log::warn(QStringLiteral("WindowManager.openSettings: windows arrive in M3"));
}

void WindowManagerStub::openEditor()
{
    cwn::Log::warn(QStringLiteral("WindowManager.openEditor: windows arrive in M3"));
}

void WindowManagerStub::openPluginPlaza()
{
    cwn::Log::warn(QStringLiteral("WindowManager.openPluginPlaza: plugin system is Phase 2"));
}

void WindowManagerStub::openWhatsNew()
{
    cwn::Log::warn(QStringLiteral("WindowManager.openWhatsNew: windows arrive in M3"));
}

void WindowManagerStub::openTutorial()
{
    cwn::Log::warn(QStringLiteral("WindowManager.openTutorial: windows arrive in M3"));
}

void WindowManagerStub::openDebugger()
{
    cwn::Log::warn(QStringLiteral("WindowManager.openDebugger: debugger arrives in M3+"));
}

void WindowManagerStub::openClassSwap()
{
    cwn::Log::warn(QStringLiteral("WindowManager.openClassSwap: windows arrive in M3"));
}

void WindowManagerStub::openSingleInstanceDialog()
{
    cwn::Log::warn(QStringLiteral("WindowManager.openSingleInstanceDialog: arrives in M3"));
}

ClassSwapManagerStub::ClassSwapManagerStub(QObject *parent)
    : QObject(parent)
{
}

UtilsBackendStub::UtilsBackendStub(QObject *parent)
    : QObject(parent)
{
}

bool UtilsBackendStub::executeShortcut(const QString &shortcutId)
{
    Q_UNUSED(shortcutId);
    return false;
}

void UtilsBackendStub::moveShortcutTo(const QString &shortcutId, int index)
{
    Q_UNUSED(shortcutId);
    Q_UNUSED(index);
}

void UtilsBackendStub::setShortcutEnabled(const QString &shortcutId, bool enabled)
{
    Q_UNUSED(shortcutId);
    Q_UNUSED(enabled);
}

PluginManagerStub::PluginManagerStub(QObject *parent)
    : QObject(parent)
{
}
