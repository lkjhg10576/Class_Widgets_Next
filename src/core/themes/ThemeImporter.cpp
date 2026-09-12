#include "ThemeImporter.h"

#include "../Logger.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSet>

namespace {
// 单次 tar 调用超时；主题包均为小文件
constexpr int kTarTimeoutMs = 60000;
constexpr int kTarStartTimeoutMs = 5000;
} // namespace

ThemeImporter::ThemeImporter(QObject *parent)
    : QObject(parent)
{
}

bool ThemeImporter::runTar(const QStringList &arguments, QByteArray *standardOutput,
                           QString *errorString)
{
    QProcess process;
    process.start(QStringLiteral("tar"), arguments);
    if (!process.waitForStarted(kTarStartTimeoutMs)) {
        if (errorString)
            *errorString = QStringLiteral("Failed to start system tar: %1")
                               .arg(process.errorString());
        return false;
    }
    if (!process.waitForFinished(kTarTimeoutMs)) {
        if (errorString)
            *errorString = QStringLiteral("tar timed out: %1").arg(process.errorString());
        process.kill();
        process.waitForFinished(3000);
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (errorString)
            *errorString = QStringLiteral("tar exited with code %1: %2")
                               .arg(process.exitCode())
                               .arg(QString::fromLocal8Bit(process.readAllStandardError().trimmed()));
        return false;
    }
    if (standardOutput)
        *standardOutput = process.readAllStandardOutput();
    return true;
}

QStringList ThemeImporter::listMembers(const QString &zipPath, QString *errorString)
{
    QByteArray output;
    QString error;
    if (!runTar({ QStringLiteral("-tf"), zipPath }, &output, &error)) {
        if (errorString)
            *errorString = error;
        cwn::Log::error(QStringLiteral("Failed to analyze zip file %1: %2").arg(zipPath, error));
        return {};
    }
    QStringList members;
    const QStringList lines =
        QString::fromLocal8Bit(output).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (QString line : lines) {
        line = line.trimmed();
        if (!line.isEmpty())
            members.append(line);
    }
    return members;
}

QByteArray ThemeImporter::readMember(const QString &zipPath, const QString &member, bool *ok)
{
    QByteArray output;
    QString error;
    const bool success = runTar({ QStringLiteral("-xOf"), zipPath, member }, &output, &error);
    if (ok)
        *ok = success;
    if (!success)
        cwn::Log::warn(QStringLiteral("Failed to read theme meta from %1: %2").arg(member, error));
    return output;
}

QVariantMap ThemeImporter::readThemeMeta(const QString &zipPath, const QString &member)
{
    bool ok = false;
    const QByteArray content = readMember(zipPath, member, &ok);
    if (!ok)
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(content);
    if (!doc.isObject()) {
        cwn::Log::warn(QStringLiteral("Failed to read theme meta from %1: invalid JSON").arg(member));
        return {};
    }
    return doc.object().toVariantMap();
}

QString ThemeImporter::normalizeMember(QString member)
{
    member = member.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (member.startsWith(QStringLiteral("./")))
        member.remove(0, 2);
    return member;
}

bool ThemeImporter::isUnsafeMember(const QString &member)
{
    if (member.startsWith(QLatin1Char('/')))
        return true;
    if (member == QLatin1String("..") || member.startsWith(QStringLiteral("../"))
        || member.contains(QStringLiteral("/../")))
        return true;
    if (member.contains(QLatin1Char(':'))) // Windows 盘符（C:/...）
        return true;
    return false;
}

bool ThemeImporter::removeDirInside(const QString &dir, const QString &root)
{
    if (dir.isEmpty() || root.isEmpty())
        return false;
    const QString normalizedDir = QDir::fromNativeSeparators(dir);
    const QString normalizedRoot = QDir::fromNativeSeparators(root);
    // 必须严格位于 root 之内
    if (!normalizedDir.startsWith(normalizedRoot + QLatin1Char('/')))
        return false;
    QDir target(normalizedDir);
    if (!target.exists())
        return false;
    return target.removeRecursively(); // 对应上游 shutil.rmtree
}

QVariantList ThemeImporter::checkConflicts(const QString &zipPath,
                                           const QVariantList &existingThemes,
                                           QString *errorString) const
{
    QVariantList conflicts;
    QString error;
    const QStringList members = listMembers(zipPath, &error);
    if (!error.isEmpty()) {
        if (errorString)
            *errorString = error;
        return {};
    }

    // manager.py:260-288：逐个读取 zip 内 cwtheme.json 的 id，与已安装主题比对
    for (const QString &member : members) {
        const QString normalized = normalizeMember(member);
        if (!normalized.endsWith(QLatin1String("cwtheme.json")))
            continue;

        const QVariantMap meta = readThemeMeta(zipPath, member);
        const QString themeId = meta.value(QStringLiteral("id")).toString();
        if (themeId.isEmpty())
            continue;

        for (const QVariant &t : existingThemes) {
            const QVariantMap existing = t.toMap();
            if (existing.value(QStringLiteral("id")).toString() != themeId)
                continue;
            // manager.py:276-283：记录冲突（新旧版本号供 QML 展示）
            cwn::Log::warn(QStringLiteral("Theme conflict detected: %1 (existing: %2, new: %3)")
                               .arg(themeId,
                                    existing.value(QStringLiteral("version")).toString(),
                                    meta.value(QStringLiteral("version")).toString()));
            QVariantMap conflict;
            conflict.insert(QStringLiteral("id"), themeId);
            conflict.insert(QStringLiteral("name"),
                            meta.value(QStringLiteral("name"), themeId));
            conflict.insert(QStringLiteral("version"),
                            meta.value(QStringLiteral("version"), QStringLiteral("unknown")));
            conflict.insert(QStringLiteral("existing_version"),
                            existing.value(QStringLiteral("version"),
                                           QStringLiteral("unknown")));
            conflict.insert(QStringLiteral("meta"), meta);
            conflicts.append(conflict);
            break;
        }
    }
    return conflicts;
}

bool ThemeImporter::extractZip(const QString &zipPath, const QString &themesRoot,
                               const QVariantList &existingThemes, QString *errorString)
{
    QString error;
    const QStringList members = listMembers(zipPath, &error);
    if (!error.isEmpty()) {
        if (errorString)
            *errorString = error;
        return false;
    }
    if (members.isEmpty()) {
        if (errorString)
            *errorString = QStringLiteral("Archive is empty: %1").arg(zipPath);
        return false;
    }

    // zip-slip 防护：拒绝绝对路径 / .. 上跳 / 盘符（bsdtar 有部分防护，这里显式拦截）
    for (const QString &member : members) {
        const QString normalized = normalizeMember(member);
        if (normalized.isEmpty() || normalized.endsWith(QLatin1Char('/')))
            continue; // 目录条目
        if (isUnsafeMember(normalized)) {
            if (errorString)
                *errorString = QStringLiteral("Unsafe path in archive: %1").arg(normalized);
            cwn::Log::error(QStringLiteral("Unsafe path in archive: %1").arg(normalized));
            return false;
        }
    }

    // worker.py:29-34：有冲突先删现有主题目录。上游用「成员路径 == 主题 id」比较，
    // 实际恒不成立（缺陷）；这里按其意图实现——zip 内 cwtheme.json 的 id 若与已安装
    // 主题相同，则先删除对应旧目录，保证升级导入干净生效。
    QStringList zipThemeIds;
    for (const QString &member : members) {
        const QString normalized = normalizeMember(member);
        if (!normalized.endsWith(QLatin1String("cwtheme.json")))
            continue;
        const QString id = readThemeMeta(zipPath, member)
                               .value(QStringLiteral("id")).toString();
        if (!id.isEmpty())
            zipThemeIds.append(id);
    }
    if (!zipThemeIds.isEmpty()) {
        for (const QVariant &t : existingThemes) {
            const QVariantMap existing = t.toMap();
            if (!zipThemeIds.contains(existing.value(QStringLiteral("id")).toString()))
                continue;
            const QString oldDir = existing.value(QStringLiteral("_path")).toString();
            if (removeDirInside(oldDir, themesRoot)) {
                cwn::Log::info(QStringLiteral("Removed existing theme directory for update: %1")
                                   .arg(oldDir));
            }
        }
    }

    // worker.py:36-42：顶层目录分流——单顶层目录直接解到 themesRoot；
    // 多顶层目录则解到 themesRoot/<zip 文件名 stem>（已存在则先清空）
    QSet<QString> topDirs;
    for (const QString &member : members) {
        const QString normalized = normalizeMember(member);
        if (normalized.isEmpty() || normalized.endsWith(QLatin1Char('/')))
            continue;
        const int slash = normalized.indexOf(QLatin1Char('/'));
        topDirs.insert(slash < 0 ? normalized : normalized.left(slash));
    }
    QString destination = themesRoot;
    if (topDirs.size() != 1) {
        destination = themesRoot + QLatin1Char('/')
                      + QFileInfo(zipPath).completeBaseName(); // 等价 Python Path.stem
        removeDirInside(destination, themesRoot);
    }
    if (!QDir().mkpath(destination)) {
        if (errorString)
            *errorString = QStringLiteral("Failed to create directory: %1").arg(destination);
        return false;
    }

    if (!runTar({ QStringLiteral("-xf"), zipPath, QStringLiteral("-C"), destination },
                nullptr, &error)) {
        if (errorString)
            *errorString = error;
        cwn::Log::error(QStringLiteral("Theme import error: %1").arg(error));
        return false;
    }
    return true;
}
