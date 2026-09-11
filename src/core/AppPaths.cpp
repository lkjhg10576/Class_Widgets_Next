#include "AppPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

namespace {
constexpr auto kRootMarker = "src/qml/MainInterface.qml";
constexpr auto kRootEnvVar = "CW2_APP_ROOT";

QString normalizeDir(const QString &path)
{
    QDir d(path);
    return d.absolutePath();
}
} // namespace

AppPaths::AppPaths(QObject *parent)
    : QObject(parent)
{
}

AppPaths &AppPaths::instance()
{
    static AppPaths s_instance;
    return s_instance;
}

void AppPaths::initialize()
{
    m_root = resolveRoot();
}

QString AppPaths::resolveRoot() const
{
    const QString envRoot = qEnvironmentVariable(kRootEnvVar);
    if (!envRoot.isEmpty()) {
        const QString candidate = normalizeDir(envRoot);
        if (QFileInfo::exists(candidate + QLatin1Char('/') + kRootMarker))
            return candidate;
        qWarning("CW2_APP_ROOT=%s does not contain %s, falling back to executable search",
                 qUtf8Printable(envRoot), kRootMarker);
    }

    QDir dir(QCoreApplication::applicationDirPath());
    for (int depth = 0; depth < 6; ++depth) {
        if (QFileInfo::exists(dir.absoluteFilePath(QLatin1String(kRootMarker))))
            return dir.absolutePath();
        if (!dir.cdUp())
            break;
    }

    const QString fallback = QCoreApplication::applicationDirPath();
    qWarning("AppPaths: runtime root marker '%s' not found near the executable; "
             "using %s as root",
             kRootMarker, qUtf8Printable(fallback));
    return fallback;
}

QString AppPaths::src() const
{
    return normalizeDir(m_root + QStringLiteral("/src"));
}

QString AppPaths::qmlRoot() const
{
    return normalizeDir(src() + QStringLiteral("/qml"));
}

QString AppPaths::cwRoot() const
{
    return normalizeDir(qmlRoot() + QStringLiteral("/ClassWidgets"));
}

QString AppPaths::assetsRoot() const
{
    return normalizeDir(m_root + QStringLiteral("/assets"));
}

QString AppPaths::themesRoot() const
{
    return normalizeDir(m_root + QStringLiteral("/themes"));
}

QString AppPaths::configsRoot() const
{
    return normalizeDir(m_root + QStringLiteral("/configs"));
}

QString AppPaths::logsRoot() const
{
    return normalizeDir(m_root + QStringLiteral("/logs"));
}

QString AppPaths::rinUiRoot() const
{
    return normalizeDir(m_root + QStringLiteral("/RinUI"));
}

QString AppPaths::examplesRoot() const
{
    return normalizeDir(m_root + QStringLiteral("/examples"));
}

QString AppPaths::uriOf(const QString &localPath) const
{
    return QUrl::fromLocalFile(QFileInfo(localPath).absoluteFilePath()).toString();
}

QString AppPaths::root(const QString &path) const
{
    return uriOf(m_root + QLatin1Char('/') + path);
}

QString AppPaths::assets(const QString &path) const
{
    return uriOf(assetsRoot() + QLatin1Char('/') + path);
}

QString AppPaths::qml(const QString &path) const
{
    return uriOf(cwRoot() + QLatin1Char('/') + path);
}

QString AppPaths::images(const QString &path) const
{
    return uriOf(assetsRoot() + QStringLiteral("/images/") + path);
}
