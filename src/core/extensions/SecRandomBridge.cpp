#include "SecRandomBridge.h"

#include "Logger.h"

#include <QFile>

#ifdef Q_OS_WIN
#include <QDir>
#include <QSettings>
#include <QTimer>
#endif

SecRandomBridge::SecRandomBridge(QObject *parent)
    : QObject(parent)
{
}

void SecRandomBridge::probe()
{
#ifdef Q_OS_WIN
    // Windows 探测：HKCU/HKLM 注册表 + C–J 盘扫描（只读，不写注册表/不装驱动）。
    // 失败隔离：任何异常只打日志，available 保持 false，设置页回退 builtin。
    try {
        QString installDir;
        const QStringList regPaths = {
            QStringLiteral("HKEY_CURRENT_USER\\Software\\SecRandom"),
            QStringLiteral("HKEY_LOCAL_MACHINE\\Software\\SecRandom"),
            QStringLiteral("HKEY_CURRENT_USER\\Software\\Seewo\\SecRandom"),
            QStringLiteral("HKEY_LOCAL_MACHINE\\Software\\Seewo\\SecRandom"),
        };
        for (const QString &path : regPaths) {
            QSettings settings(path, QSettings::NativeFormat);
            const QString dir = settings.value(QStringLiteral("InstallDir")).toString().trimmed();
            if (!dir.isEmpty() && QDir(dir).exists()) {
                installDir = dir;
                break;
            }
        }
        if (installDir.isEmpty()) {
            for (char drive = 'C'; drive <= 'J'; ++drive) {
                const QString base = QStringLiteral("%1:/SecRandom").arg(QChar(drive));
                if (QDir(base).exists()) {
                    installDir = base;
                    break;
                }
            }
        }
        if (installDir.isEmpty()) {
            setAvailable(false, QString(), {});
            return;
        }
        // 版本判定：优先读安装目录 version.txt/version.ini，回退目录名解析
        QString version = QStringLiteral("unknown");
        for (const QString &name : { QStringLiteral("version.txt"), QStringLiteral("version.ini") }) {
            QFile file(installDir + QLatin1Char('/') + name);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                version = QString::fromUtf8(file.readAll()).trimmed().left(32);
                if (!version.isEmpty())
                    break;
            }
        }
        // 三格式记录路径（只读监听，不播历史）
        QStringList paths = {
            installDir + QStringLiteral("/history"),
            installDir + QStringLiteral("/roll_call_record_default.json"),
        };
        setAvailable(true, version, paths);
        cwn::Log::info(QStringLiteral("SecRandomBridge: detected at %1 (version=%2)")
                           .arg(installDir, version));
    } catch (...) {
        cwn::Log::warn(QStringLiteral("SecRandomBridge: probe failed (isolated)"));
        setAvailable(false, QString(), {});
    }
#else
    // 非 Windows：恒不可用（Linux 构建/CI 走此分支）
    setAvailable(false, QString(), {});
#endif
}

void SecRandomBridge::setWatchEnabled(bool enabled)
{
    if (m_watchEnabled == enabled)
        return;
    m_watchEnabled = enabled;
#ifdef Q_OS_WIN
    // 二期完整实现挂载点：1500ms 快照基线轮询只读监听（本桩仅记录开关，不解析）。
    cwn::Log::info(QStringLiteral("SecRandomBridge: watch %1")
                       .arg(enabled ? QStringLiteral("on") : QStringLiteral("off")));
#else
    Q_UNUSED(enabled)
#endif
}

void SecRandomBridge::setAvailable(bool available, const QString &version, const QStringList &paths)
{
    if (m_available == available && m_version == version && m_logPaths == paths)
        return;
    m_available = available;
    m_version = version;
    m_logPaths = paths;
    emit availabilityChanged();
}
