#pragma once

#include <QObject>
#include <QString>

// 对应上游 src/core/directories.py 的路径布局与 PathManager 契约。
//
// 运行时根（等价于上游 ROOT_PATH）查找顺序：
//   1. 环境变量 CW2_APP_ROOT（开发期直接指向仓库的 app/ 目录，避免拷贝）
//   2. 从可执行文件目录向上逐级查找标记文件 src/qml/MainInterface.qml
//   3. 回退到可执行文件目录自身
//
// 目录映射（上游 directories.py:5-22）：
//   root            = app/
//   src             = root/src            （上游 SRC_PATH）
//   qmlRoot         = root/src/qml        （上游 QML_PATH，DEFAULT_THEME 也指向这里）
//   cwRoot          = root/src/qml/ClassWidgets（上游 CW_PATH）
//   assetsRoot      = root/assets
//   themesRoot      = root/themes         （外部主题扫描目录）
//   configsRoot     = root/configs
//   logsRoot        = root/logs
//   rinUiRoot       = root/RinUI          （vendored RinUI QML 模块，随发行包分发）
class AppPaths : public QObject
{
    Q_OBJECT
public:
    static AppPaths &instance();

    // 必须在 QGuiApplication 创建之后、任何路径使用之前调用一次
    void initialize();

    QString root() const { return m_root; }
    QString src() const;
    QString qmlRoot() const;
    QString cwRoot() const;
    QString assetsRoot() const;
    QString themesRoot() const;
    QString configsRoot() const;
    QString logsRoot() const;
    QString rinUiRoot() const;
    QString examplesRoot() const;

    // 把本地绝对路径转成 file:/// URI（对应 Path.as_uri()）
    QString uriOf(const QString &localPath) const;

    // --- QML 侧 PathManager 契约（directories.py:39-53，必须返回 URI 字符串）---
    Q_INVOKABLE QString root(const QString &path) const;
    Q_INVOKABLE QString assets(const QString &path) const;
    Q_INVOKABLE QString qml(const QString &path) const;
    Q_INVOKABLE QString images(const QString &path) const;

private:
    explicit AppPaths(QObject *parent = nullptr);
    QString resolveRoot() const;

    QString m_root;
};
