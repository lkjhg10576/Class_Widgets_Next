#include "CWThemeManager.h"

#include "AppPaths.h"
#include "Logger.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

CWThemeManager::CWThemeManager(const AppPaths *paths, QObject *parent)
    : QObject(parent)
    , m_paths(paths)
{
}

void CWThemeManager::addTheme(const QVariantMap &meta, const QString &path, const QString &type)
{
    QVariantMap theme = meta;
    theme.insert(QStringLiteral("_type"), type);
    theme.insert(QStringLiteral("_path"), path);
    theme.insert(QStringLiteral("_compatible"), true); // 严格 api_version 校验在 M3 落地
    m_themes.append(theme);
}

void CWThemeManager::load()
{
    m_themes.clear();

    // 内置主题（对应 src/themes/__init__.py BUILTIN_THEMES）。
    // default 主题的 path 就是整个 src/qml（DEFAULT_THEME = QML_PATH）。
    QVariantMap defaultMeta;
    defaultMeta.insert(QStringLiteral("id"), QStringLiteral("com.classwidgets.default"));
    defaultMeta.insert(QStringLiteral("name"), tr("Default"));
    defaultMeta.insert(QStringLiteral("description"), tr("Class Widgets Builtin Default Theme"));
    defaultMeta.insert(QStringLiteral("author"), QStringLiteral("Class Widgets Official"));
    defaultMeta.insert(QStringLiteral("version"), QStringLiteral("1.0.0"));
    defaultMeta.insert(QStringLiteral("api_version"), QStringLiteral("*"));
    defaultMeta.insert(QStringLiteral("color"), QStringLiteral("#4099b2"));
    addTheme(defaultMeta, m_paths->qmlRoot(), QStringLiteral("builtin"));

    // 外部主题：扫描 <root>/themes/*/cwtheme.json（loader.py 要求 id/name/version/api_version/author）
    const QDir themesDir(m_paths->themesRoot());
    const QFileInfoList entries =
        themesDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &dirInfo : entries) {
        const QString cwthemePath = dirInfo.absoluteFilePath() + QStringLiteral("/cwtheme.json");
        QFile file(cwthemePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        if (!doc.isObject()) {
            cwn::Log::warn(QStringLiteral("Invalid cwtheme.json: %1").arg(cwthemePath));
            continue;
        }
        QVariantMap meta = doc.object().toVariantMap();
        if (meta.value(QStringLiteral("id")).toString().isEmpty()) {
            cwn::Log::warn(QStringLiteral("Theme missing id: %1").arg(cwthemePath));
            continue;
        }
        addTheme(meta, dirInfo.absoluteFilePath(), QStringLiteral("external"));
    }

    if (m_currentTheme.isEmpty())
        m_currentTheme = defaultThemeId();
    emit themeListChanged();
}

void CWThemeManager::applyConfiguredTheme(const QString &themeId)
{
    if (!themeId.isEmpty() && isThemePathValid(themeId)) {
        m_currentTheme = themeId;
    } else {
        cwn::Log::warn(QStringLiteral("Current theme '%1' missing or invalid, "
                                      "falling back to default theme")
                           .arg(themeId));
        m_currentTheme = defaultThemeId();
    }
}

QVariantMap CWThemeManager::getThemeById(const QString &themeId) const
{
    for (const QVariant &t : m_themes) {
        const QVariantMap theme = t.toMap();
        if (theme.value(QStringLiteral("id")).toString() == themeId)
            return theme;
    }
    return {};
}

QString CWThemeManager::getThemePath(const QString &themeId) const
{
    return getThemeById(themeId).value(QStringLiteral("_path")).toString();
}

QString CWThemeManager::getThemeType(const QString &themeId) const
{
    return getThemeById(themeId).value(QStringLiteral("_type")).toString();
}

bool CWThemeManager::isBuiltinTheme(const QString &themeId) const
{
    return getThemeType(themeId) == QStringLiteral("builtin");
}

bool CWThemeManager::isExternalTheme(const QString &themeId) const
{
    return getThemeType(themeId) == QStringLiteral("external");
}

bool CWThemeManager::isThemePathValid(const QString &themeId) const
{
    const QString themePath = getThemePath(themeId);
    if (themePath.isEmpty()) {
        cwn::Log::warn(QStringLiteral("Theme '%1' path is empty").arg(themeId));
        return false;
    }
    const QFileInfo info(themePath);
    if (!info.exists() || !info.isDir()) {
        cwn::Log::error(QStringLiteral("Theme path invalid: %1").arg(themePath));
        return false;
    }
    return true;
}

bool CWThemeManager::themeChange(const QString &themeId)
{
    if (themeId == m_currentTheme)
        return true;
    m_currentTheme = themeId;
    emit themeChanged();
    return true;
}
