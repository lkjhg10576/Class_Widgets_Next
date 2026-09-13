#include "CWThemeManager.h"

#include "AppPaths.h"
#include "ConfigStore.h"
#include "Logger.h"
#include "themes/ThemeImporter.h"
#include "themes/ThemeLoader.h"
#include "themes/ThemeUrlInterceptor.h"
#include "NativeFileDialog.h" // B4：原生文件对话框替代 QFileDialog（去 Qt6::Widgets）

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QQmlAbstractUrlInterceptor>
#include <QUrl>

CWThemeManager::CWThemeManager(const AppPaths *paths, QObject *parent)
    : QObject(parent)
    , m_paths(paths)
{
    m_loader = new ThemeLoader(this);
    m_interceptor = new ThemeUrlInterceptor(this);
    m_importer = new ThemeImporter(this);

    // manager.py:35-38：单发冷却定时器，防止主题抖动连切
    m_cooldown.setSingleShot(true);
    m_cooldown.setInterval(500);
    connect(&m_cooldown, &QTimer::timeout, this, [this] {
        // manager.py:175-178 _apply_pending：冷却结束后应用待定切换
        if (m_pending.isEmpty())
            return;
        const QString pending = m_pending;
        m_pending.clear();
        applyTheme(pending);
    });
}

void CWThemeManager::setConfigStore(ConfigStore *configs)
{
    m_configs = configs;
}

QQmlAbstractUrlInterceptor *CWThemeManager::urlInterceptor() const
{
    return m_interceptor;
}

QString CWThemeManager::getAPIVersion() const
{
    return ThemeLoader::appApiVersion();
}

bool CWThemeManager::isValidThemeId(const QString &themeId) const
{
    for (const QVariant &t : m_themes) {
        if (t.toMap().value(QStringLiteral("id")).toString() == themeId)
            return true;
    }
    return false;
}

void CWThemeManager::writeConfigCurrentTheme(const QString &themeId)
{
    if (!m_configs)
        return;
    // ConfigStore::set 自带锁定拦截，双保险（上游直接赋值 preferences.current_theme）
    m_configs->set(QStringLiteral("preferences.current_theme"), themeId);
}

void CWThemeManager::updateInterceptorTheme()
{
    // 对应 core.py:80-83/114-124：空路径时 ThemeUrlInterceptor 自动解除拦截
    m_interceptor->setThemePath(getThemePath(m_currentTheme));
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
    // manager.py:92-109 isThemePathValid
    const QString themePath = getThemePath(themeId);
    if (themePath.isEmpty()) {
        cwn::Log::warn(QStringLiteral("Theme '%1' path is empty").arg(themeId));
        return false;
    }
    const QFileInfo info(themePath);
    if (!info.exists()) {
        cwn::Log::error(QStringLiteral("Theme path does not exist: %1").arg(themePath));
        return false;
    }
    if (!info.isDir()) {
        cwn::Log::error(QStringLiteral("Theme path is not a directory: %1").arg(themePath));
        return false;
    }
    return true;
}

void CWThemeManager::rescan()
{
    m_themes = m_loader->scanThemes(); // manager.py:164

    if (m_currentTheme.isEmpty())
        m_currentTheme = defaultThemeId(); // 首次扫描先占位，applyConfiguredTheme 再校正

    // manager.py:167-170：当前主题失效 → 回退默认主题并回写配置
    bool fallback = false;
    if (!isValidThemeId(m_currentTheme)) {
        cwn::Log::warn(QStringLiteral("Current theme '%1' is invalid, falling back to default theme")
                           .arg(m_currentTheme));
        m_currentTheme = defaultThemeId();
        writeConfigCurrentTheme(m_currentTheme);
        fallback = true;
    }
    updateInterceptorTheme(); // 先就位拦截器，再广播信号（监听方可能据此重建 UI）

    emit themeListChanged(); // manager.py:172
    emit themeChanged();     // manager.py:173（重扫后 UI 重建，含导入/卸载场景）
}

void CWThemeManager::load()
{
    rescan(); // manager.py:48-50 load → scan
}

void CWThemeManager::applyConfiguredTheme(const QString &themeId)
{
    QString target = themeId;
    if (target.isEmpty() || !isValidThemeId(target) || !isThemePathValid(target)) {
        cwn::Log::warn(QStringLiteral("Current theme '%1' is invalid, falling back to default theme")
                           .arg(themeId));
        target = defaultThemeId();
        writeConfigCurrentTheme(target); // manager.py:170 的回写
    }

    const bool changed = target != m_currentTheme;
    m_currentTheme = target;
    updateInterceptorTheme();
    if (changed)
        emit themeChanged();
}

bool CWThemeManager::themeChange(const QString &themeId)
{
    // manager.py:117-119
    if (themeId == m_currentTheme)
        return true;

    // manager.py:121-123：未知主题拒绝
    if (!isValidThemeId(themeId)) {
        cwn::Log::warn(QStringLiteral("Unknown theme: %1").arg(themeId));
        return false;
    }

    // manager.py:125-128：冷却期内记为待定，冷却结束自动应用
    if (m_cooldown.isActive()) {
        m_pending = themeId;
        return true;
    }

    applyTheme(themeId);
    m_cooldown.start();
    return true;
}

void CWThemeManager::applyTheme(const QString &themeId)
{
    // manager.py:190-192：锁定键保护（ConfigStore::set 内部还有一道）
    if (m_configs && m_configs->isKeyLocked(QStringLiteral("preferences.current_theme"))) {
        cwn::Log::warn(QStringLiteral("Attempt to modify locked config key: "
                                      "preferences.current_theme. Blocked."));
        return;
    }

    QString target = themeId;
    if (!isValidThemeId(target)) {
        cwn::Log::warn(QStringLiteral("Theme '%1' is invalid, falling back to default theme")
                           .arg(target));
        target = defaultThemeId();
    }

    // manager.py:197-204：验证主题路径存在，否则回退默认主题
    if (!isThemePathValid(target)) {
        cwn::Log::error(QStringLiteral("Theme '%1' path is invalid, falling back to default theme")
                            .arg(target));
        target = defaultThemeId();
        if (!isThemePathValid(target)) {
            cwn::Log::error(QStringLiteral("Default theme '%1' path is also invalid! "
                                           "This should not happen.")
                                .arg(defaultThemeId()));
            return;
        }
    }

    m_currentTheme = target;
    writeConfigCurrentTheme(target); // manager.py:207
    cwn::Log::info(QStringLiteral("Theme switched to %1 (type: %2)")
                       .arg(target, getThemeType(target)));
    updateInterceptorTheme();
    emit themeChanged(); // manager.py:209（WidgetsWindow 据此清缓存并重建）
}

bool CWThemeManager::rollbackToDefault(const QString &failedThemeId)
{
    // manager.py:136-142：过期的失败报告（目标已不是当前主题）直接忽略
    if (!failedThemeId.isEmpty() && failedThemeId != m_currentTheme) {
        cwn::Log::info(QStringLiteral("Ignoring rollback for stale theme failure: %1 (current: %2)")
                           .arg(failedThemeId, m_currentTheme));
        return m_currentTheme == defaultThemeId();
    }

    if (!isValidThemeId(defaultThemeId())) {
        cwn::Log::error(QStringLiteral("Default theme '%1' is not available")
                            .arg(defaultThemeId()));
        return false;
    }
    if (!isThemePathValid(defaultThemeId())) {
        cwn::Log::error(QStringLiteral("Default theme '%1' path is invalid")
                            .arg(defaultThemeId()));
        return false;
    }

    m_pending.clear(); // manager.py:152

    if (m_currentTheme == defaultThemeId()) {
        // manager.py:153-158：组件失败可能发生在默认主题已是当前主题时；
        // 再次发射让窗口在清空 QML 组件缓存后重建
        emit themeChanged();
        return true;
    }

    applyTheme(defaultThemeId()); // manager.py:160
    return m_currentTheme == defaultThemeId();
}

QVariantList CWThemeManager::importTheme()
{
    // manager.py:212-245
    cwn::Log::info(QStringLiteral("Starting theme import process..."));
    const QString zipPath = NativeFileDialog::getOpenFileName(
        QStringLiteral("Import Theme"), QString(),
        QStringLiteral("Class Widgets Theme (*.cwtheme);;Theme ZIP (*.zip)"));
    if (zipPath.isEmpty()) {
        cwn::Log::info(QStringLiteral("Theme import cancelled by user"));
        return {};
    }
    cwn::Log::info(QStringLiteral("Selected theme file: %1").arg(zipPath));
    cwn::Log::info(QStringLiteral("Checking for theme conflicts..."));

    const QVariantList conflicts = checkThemeConflicts(zipPath);
    if (!conflicts.isEmpty()) {
        // manager.py:236-240：有冲突 → 返回冲突信息（附 zip_path）供 QML 确认对话框
        cwn::Log::warn(QStringLiteral("Found %1 conflicting theme(s)").arg(conflicts.size()));
        QVariantList withPaths;
        for (const QVariant &c : conflicts) {
            QVariantMap conflict = c.toMap();
            conflict.insert(QStringLiteral("zip_path"), zipPath);
            withPaths.append(conflict);
        }
        return withPaths;
    }

    cwn::Log::info(QStringLiteral("No conflicts found, proceeding with direct import"));
    importThemeWithPath(zipPath); // manager.py:243-245
    return {};
}

QVariantList CWThemeManager::checkThemeConflicts(const QString &zipPath)
{
    // manager.py:294-297 checkThemeConflicts → get_conflicting_themes
    QString error;
    const QVariantList conflicts = m_importer->checkConflicts(zipPath, m_themes, &error);
    if (!error.isEmpty())
        cwn::Log::error(QStringLiteral("Failed to analyze zip file %1: %2").arg(zipPath, error));
    return conflicts;
}

bool CWThemeManager::importThemeWithPath(const QString &zipPath)
{
    // manager.py:299-346 importThemeWithPath（worker 同步化改写，见 ThemeImporter）
    if (zipPath.isEmpty())
        return false;

    // worker.py:22-23：记录导入前的 id 集合
    QStringList oldIds;
    for (const QVariant &t : m_themes)
        oldIds.append(t.toMap().value(QStringLiteral("id")).toString());

    QString error;
    if (!m_importer->extractZip(zipPath, m_paths->themesRoot(), m_themes, &error)) {
        // manager.py:335-340 on_error
        cwn::Log::error(QStringLiteral("Theme import error: %1").arg(error));
        emit themeImportFailed(error);
        return false;
    }

    rescan(); // worker.py:44 scan_func

    // worker.py:45-55：new = 新旧差集；updated = 新旧交集（含未变动主题，忠实移植）
    QStringList newThemes;
    QStringList updatedThemes;
    for (const QVariant &t : m_themes) {
        const QString id = t.toMap().value(QStringLiteral("id")).toString();
        if (!oldIds.contains(id))
            newThemes.append(id);
        else
            updatedThemes.append(id);
    }

    if (!newThemes.isEmpty() || !updatedThemes.isEmpty()) {
        if (!updatedThemes.isEmpty())
            cwn::Log::info(QStringLiteral("Updated theme(s): %1")
                               .arg(updatedThemes.join(QLatin1String(", "))));
        if (!newThemes.isEmpty())
            cwn::Log::info(QStringLiteral("Imported new theme(s): %1")
                               .arg(newThemes.join(QLatin1String(", "))));
        emit themeListChanged(); // manager.py:323（rescan 已发过一次，保留上游的双发）
        emit themeImportSucceeded();
    } else {
        // manager.py:326-330：包内没有可识别主题
        cwn::Log::warn(QStringLiteral("No themes were imported from: %1").arg(zipPath));
        emit themeImportFailed(QStringLiteral("No valid theme found in archive."));
    }
    return true;
}

bool CWThemeManager::openThemeFolder(const QString &themeId)
{
    // manager.py:348-368
    const QVariantMap meta = getThemeById(themeId);
    if (meta.isEmpty()) {
        cwn::Log::warn(QStringLiteral("Theme %1 not found, cannot open folder.").arg(themeId));
        return false;
    }
    const QString folderPath = meta.value(QStringLiteral("_path")).toString();
    if (folderPath.isEmpty() || !QFileInfo::exists(folderPath)) {
        cwn::Log::warn(QStringLiteral("Theme folder %1 does not exist.").arg(folderPath));
        return false;
    }
    const bool success = QDesktopServices::openUrl(QUrl::fromLocalFile(folderPath));
    if (!success)
        cwn::Log::error(QStringLiteral("Failed to open theme folder: %1").arg(folderPath));
    return success;
}

bool CWThemeManager::uninstallTheme(const QString &themeId)
{
    // manager.py:370-403
    const QVariantMap meta = getThemeById(themeId);
    if (meta.isEmpty()) {
        cwn::Log::warn(QStringLiteral("Theme %1 not found, cannot uninstall.").arg(themeId));
        return false;
    }

    // manager.py:380-383：内置主题不可卸载
    if (meta.value(QStringLiteral("_type")).toString() == QLatin1String("builtin")) {
        cwn::Log::warn(QStringLiteral("Theme %1 is builtin and cannot be uninstalled.").arg(themeId));
        return false;
    }

    // manager.py:386-389：卸载当前主题前先切回默认
    if (themeId == m_currentTheme) {
        cwn::Log::info(QStringLiteral("Uninstalling current theme %1, switching to default theme")
                           .arg(themeId));
        themeChange(defaultThemeId());
    }

    // manager.py:391-396：删除主题目录（shutil.rmtree → QDir::removeRecursively）
    const QString themeDir = meta.value(QStringLiteral("_path")).toString();
    if (!themeDir.isEmpty() && QDir(themeDir).exists()) {
        if (!QDir(themeDir).removeRecursively()) {
            cwn::Log::error(QStringLiteral("Failed to uninstall theme %1").arg(themeId));
            return false;
        }
        cwn::Log::info(QStringLiteral("Uninstalled theme %1, removed %2").arg(themeId, themeDir));
    }

    rescan(); // manager.py:398-399：重扫主题列表
    return true;
}
