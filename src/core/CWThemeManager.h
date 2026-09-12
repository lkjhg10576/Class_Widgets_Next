#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

class AppPaths;
class ConfigStore;
class QQmlAbstractUrlInterceptor;
class ThemeImporter;
class ThemeLoader;
class ThemeUrlInterceptor;

// 对应上游 core/themes/manager.py 的 ThemeManager（QML 上下文名 "CWThemeManager"）。
// M3 完整实现（此前 M1 为桩）：
//   - 主题发现：委托 ThemeLoader（loader.py，内置 5 主题 + 外部 cwtheme.json 扫描）
//   - 切换：themeChange 带 500ms 冷却与待定队列（manager.py:35-39/116-131/175-178）
//   - 应用：_apply 的锁检查/回退/配置回写（manager.py:188-209）
//   - 失败恢复：rollbackToDefault（manager.py:133-161 rollback_to_default）
//   - 导入/冲突/卸载/打开目录（manager.py:211-403，zip 经 ThemeImporter+系统 tar）
//   - URL 拦截：内置 ThemeUrlInterceptor（interceptor.py），当前主题变化即更新
//
// QML 契约（grep 实测 22 处引用 / 3 文件，全部覆盖）：
//   themes / currentTheme 属性；themeChange / getThemeById / getAPIVersion
//   MainInterface.qml:175 Connections onThemeChanged（取 getThemeById().color 应用主题色）
//   WidgetLoader.qml:73 Connections onThemeReadyToReload（清缓存重建 Loader）
class CWThemeManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentTheme READ currentTheme NOTIFY themeChanged)
    Q_PROPERTY(QVariantList themes READ themes NOTIFY themeListChanged)

public:
    explicit CWThemeManager(const AppPaths *paths, QObject *parent = nullptr);

    // manager.py:47-50 load（Slot load → scan）：扫描并确保当前主题占位有效
    void load();

    // 启动时依据配置 preferences.current_theme 设定当前主题（M1 契约，AppCentral 调用，
    // 签名不变）。对应 scan() 的校验/回退（manager.py:167-170）：无效时回退默认主题并
    // 回写配置；随后更新 URL 拦截器并在主题实际变化时发射 themeChanged
    void applyConfiguredTheme(const QString &themeId);

    // 可选装配（对应上游持有 app_central.configs）：提供后 _apply 的锁定检查与
    // 配置回写生效。AppCentral 在 initialize() 里 m_themeManager->setConfigStore(m_configs)
    void setConfigStore(ConfigStore *configs);

    QString currentTheme() const { return m_currentTheme; }
    QVariantList themes() const { return m_themes; }

    // URL 拦截器（interceptor.py）。由引擎侧安装：
    //   engine->addUrlInterceptor(m_themeManager->urlInterceptor());
    // 对应 core.py:33-34 的 engine.setUrlInterceptor（Qt6 用 addUrlInterceptor）
    QQmlAbstractUrlInterceptor *urlInterceptor() const;
    ThemeUrlInterceptor *interceptorObject() const { return m_interceptor; }

    // 上游 themes/loader.py:25 的 APP_API_VERSION
    Q_INVOKABLE QString getAPIVersion() const;
    Q_INVOKABLE QString getThemePath(const QString &themeId) const;
    Q_INVOKABLE QVariantMap getThemeById(const QString &themeId) const;
    Q_INVOKABLE QString getThemeType(const QString &themeId) const;
    Q_INVOKABLE bool isBuiltinTheme(const QString &themeId) const;
    Q_INVOKABLE bool isExternalTheme(const QString &themeId) const;
    Q_INVOKABLE bool isThemePathValid(const QString &themeId) const;
    // manager.py:116-131：未知主题返回 false；冷却期内记为待定切换
    Q_INVOKABLE bool themeChange(const QString &themeId);

    // manager.py:133-161 rollback_to_default：主题加载失败后回退默认主题；
    // 默认主题已选中时再次发射 themeChanged 以触发窗口在清缓存后重建
    Q_INVOKABLE bool rollbackToDefault(const QString &failedThemeId = QString());

    // ---- 导入/卸载（manager.py:211-403）----
    // manager.py:212-245 importTheme：文件对话框选包（*.cwtheme/*.zip）；返回冲突列表
    //（空 = 已直接导入或用户取消；冲突项含 zip_path 供 QML 确认对话框）
    Q_INVOKABLE QVariantList importTheme();
    // manager.py:294-297 checkThemeConflicts：检测 zip 内主题与已安装主题的 id 冲突
    Q_INVOKABLE QVariantList checkThemeConflicts(const QString &zipPath);
    // manager.py:299-346 importThemeWithPath：解压导入并重扫；同步化改写（见 ThemeImporter），
    // 失败时返回 false 并发射 themeImportFailed
    Q_INVOKABLE bool importThemeWithPath(const QString &zipPath);
    // manager.py:348-368 openThemeFolder：QDesktopServices 打开主题目录
    Q_INVOKABLE bool openThemeFolder(const QString &themeId);
    // manager.py:370-403 uninstallTheme：内置主题拒绝；当前主题卸载前先切默认
    Q_INVOKABLE bool uninstallTheme(const QString &themeId);

    // 上游 DEFAULT_THEME_ID（manager.py:19）
    static QString defaultThemeId() { return QStringLiteral("com.classwidgets.default"); }

signals:
    void themeChanged();
    void themeListChanged();
    void themeReadyToReload(); // WidgetLoader.qml:73 依赖（发射方为 WidgetsWindow）
    // 上游无此信号（M1 为 AddWidgetsDialog 预留）；保留以兼容
    void themeReloadStarted();
    void themeImportSucceeded();
    void themeImportFailed(const QString &message);

private:
    // manager.py:188-209 _apply：锁检查 → 有效性回退 → 写配置 → themeChanged
    void applyTheme(const QString &themeId);
    // manager.py:163-173 scan：重扫列表 + 当前主题有效性回退
    void rescan();
    // manager.py:180-181 _is_theme_valid：id 是否在已发现列表中
    bool isValidThemeId(const QString &themeId) const;
    void writeConfigCurrentTheme(const QString &themeId);
    // core.py:80-83/114-124 set_theme 的集中化：当前主题变化即更新拦截器路径
    void updateInterceptorTheme();

    const AppPaths *m_paths;
    ConfigStore *m_configs = nullptr;
    ThemeLoader *m_loader = nullptr;
    ThemeUrlInterceptor *m_interceptor = nullptr;
    ThemeImporter *m_importer = nullptr;
    QVariantList m_themes;
    QString m_currentTheme;
    QTimer m_cooldown;  // manager.py:35-38：500ms 单发切换冷却
    QString m_pending;  // manager.py:39：冷却期内的待定切换
};
