#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class AppPaths;

// 对应上游 core/themes/manager.py 的 ThemeManager（QML 名 "CWThemeManager"）。
// M1 只提供主题扫描/查询/切换信号面；ZIP 导入与 URL 拦截器在 M3 落地。
//
// QML 契约：
//   currentTheme 属性（NOTIFY themeChanged）
//   getThemeById(id) → { id, name, color, ... }     MainInterface.qml 使用
//   themeReadyToReload 信号                          WidgetLoader.qml:77 依赖
class CWThemeManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentTheme READ currentTheme NOTIFY themeChanged)
    Q_PROPERTY(QVariantList themes READ themes NOTIFY themeListChanged)

public:
    explicit CWThemeManager(const AppPaths *paths, QObject *parent = nullptr);

    // 内置主题 + 扫描 <root>/themes/*/cwtheme.json（对应 loader.py scan）
    void load();

    // 依据配置里的 current_theme 设定当前主题；无效时回退内置默认主题
    void applyConfiguredTheme(const QString &themeId);

    QString currentTheme() const { return m_currentTheme; }
    QVariantList themes() const { return m_themes; }

    // 上游 themes/loader.py 的 APP_API_VERSION
    Q_INVOKABLE QString getAPIVersion() const { return QStringLiteral("2.0.0"); }
    Q_INVOKABLE QString getThemePath(const QString &themeId) const;
    Q_INVOKABLE QVariantMap getThemeById(const QString &themeId) const;
    Q_INVOKABLE QString getThemeType(const QString &themeId) const;
    Q_INVOKABLE bool isBuiltinTheme(const QString &themeId) const;
    Q_INVOKABLE bool isExternalTheme(const QString &themeId) const;
    Q_INVOKABLE bool isThemePathValid(const QString &themeId) const;
    Q_INVOKABLE bool themeChange(const QString &themeId);

    static QString defaultThemeId() { return QStringLiteral("com.classwidgets.default"); }

signals:
    void themeChanged();
    void themeListChanged();
    void themeReadyToReload();
    // AddWidgetsDialog.qml 的 onThemeReloadStarted 处理器依赖此信号
    void themeReloadStarted();
    void themeImportSucceeded();
    void themeImportFailed(const QString &message);

private:
    void addTheme(const QVariantMap &meta, const QString &path, const QString &type);

    const AppPaths *m_paths;
    QVariantList m_themes;
    QString m_currentTheme;
};
