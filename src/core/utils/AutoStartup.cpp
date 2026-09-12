#include "AutoStartup.h"

#include "../Logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QLatin1String>
#include <QSettings>
#include <QString>

namespace {

// auto_startup.py:30 注册表 Run 键（QSettings NativeFormat 直连注册表路径）
const QLatin1String kRunKey(
    "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");

// auto_startup.py:8 APP_NAME = src/__init__.py:6 __app_name__
QString appName()
{
    return QStringLiteral("ClassWidgets_2");
}

// auto_startup.py:9 APP_PATH（打包形态 = sys.executable 的 C++ 等价物）
QString appPath()
{
    return QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
}

} // namespace

namespace AutoStartup {

bool supported()
{
    // auto_startup.py:16-18
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

bool enable()
{
    // auto_startup.py:21-38
    if (!supported()) {
        cwn::Log::warn(QStringLiteral("Autostart is only supported on Windows"));
        return false;
    }
    QSettings settings(QString(kRunKey), QSettings::NativeFormat);
    settings.setValue(appName(), appPath());
    if (settings.status() != QSettings::NoError) {
        cwn::Log::error(QStringLiteral("Failed to enable autostart (QSettings status %1)")
                            .arg(int(settings.status())));
        return false;
    }
    cwn::Log::info(QStringLiteral("%1 autostart enabled").arg(appName()));
    return true;
}

bool disable()
{
    // auto_startup.py:41-60：键不存在时 QSettings::remove 为无操作
    //（等价上游 FileNotFoundError 的 pass 分支）
    if (!supported()) {
        cwn::Log::warn(QStringLiteral("Autostart is only supported on Windows"));
        return false;
    }
    QSettings settings(QString(kRunKey), QSettings::NativeFormat);
    settings.remove(appName());
    if (settings.status() != QSettings::NoError) {
        cwn::Log::error(QStringLiteral("Failed to disable autostart (QSettings status %1)")
                            .arg(int(settings.status())));
        return false;
    }
    cwn::Log::info(QStringLiteral("%1 autostart disabled").arg(appName()));
    return true;
}

bool isEnabled()
{
    // auto_startup.py:63-83：读出的值与当前可执行路径逐字符比较
    if (!supported())
        return false;
    QSettings settings(QString(kRunKey), QSettings::NativeFormat);
    const QString stored = settings.value(appName()).toString();
    if (stored.isEmpty())
        return false;
    return stored == appPath();
}

} // namespace AutoStartup
