#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

// 对应上游 src/core/themes/worker.py 的 ThemeImportWorker 与
// manager.py:247-292 get_conflicting_themes（导入与冲突检测的 zip 机制层）。
//
// ZIP 方案说明：Qt 公开 API 没有 zip 读写，且禁止第三方库，故统一用 QProcess 调
// 系统 tar（Windows 10+ 自带 bsdtar/libarchive，支持 zip 容器）：
//   tar -tf  <zip>              列出成员（冲突检测/顶层目录判定）
//   tar -xOf <zip> <member>     读取成员内容（解析 zip 内 cwtheme.json）
//   tar -xf  <zip> -C <dir>     解压
// tar 不可用/退出码非 0 时向上返回 errorString（上游对应 worker.error 信号）。
// 上游把解压放在 QThread 里（ThemeImportWorker）；M3 改为同步执行——主题包体积小
// （秒级），换取消除跨线程对象生命周期风险，后续如需再包一层 QThread。
class ThemeImporter : public QObject
{
    Q_OBJECT
public:
    explicit ThemeImporter(QObject *parent = nullptr);

    // manager.py:247-292 get_conflicting_themes：zip 内 cwtheme.json 的 id 与
    // 已安装主题求交；冲突项键与 model.py ThemeConflict 对齐：
    //   id / name / version / existing_version / meta（zip_path 由调用方补）
    QVariantList checkConflicts(const QString &zipPath, const QVariantList &existingThemes,
                                QString *errorString = nullptr) const;

    // worker.py:19-57 run：解压 zip 到 themesRoot。
    // 含：zip-slip 防护；同 id 旧目录清理（worker.py:29-34 的意图，见 cpp 注释）；
    // 单/多顶层目录分流（worker.py:36-42）。成功返回 true。
    bool extractZip(const QString &zipPath, const QString &themesRoot,
                    const QVariantList &existingThemes, QString *errorString);

private:
    static bool runTar(const QStringList &arguments, QByteArray *standardOutput,
                       QString *errorString);
    static QStringList listMembers(const QString &zipPath, QString *errorString);
    static QByteArray readMember(const QString &zipPath, const QString &member, bool *ok);
    static QVariantMap readThemeMeta(const QString &zipPath, const QString &member);
    static QString normalizeMember(QString member);
    static bool isUnsafeMember(const QString &member);
    // 仅允许删除 themesRoot 之内的目录（防误删）；成功返回 true
    static bool removeDirInside(const QString &dir, const QString &root);
};
