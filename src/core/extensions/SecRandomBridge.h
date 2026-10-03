#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

// P2 二期 SecRandom 桥（four-plugins §4.2，仅 Windows 条件编译，失败隔离只打日志）。
// 对应上游 `com.rollcall` v2.0.1 的 `secrandom_service.py`（626 行）+ `secrandom_ipc.py`（174 行）：
// 注册表 HKCU/HKLM + 扫 C–J 盘 + 版本判定 + 三格式记录
// （`history/<班级>.json` / `roll_call_record__*.json` / `roll_call_record_default.json`）
// + 快照基线 1500ms 轮询只读监听 + `secrandom://` trigger + 分代路径 `secrandom_paths{2,3}`。
// 本期只落地桩 + 设置页三选一（builtin/2/3）：非 Windows 平台全部方法为空实现，
// Windows 平台同样只做探测与只读监听（不写历史、不播历史），保证失败隔离。
class SecRandomBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY availabilityChanged)
    Q_PROPERTY(QString version READ version NOTIFY availabilityChanged)
    Q_PROPERTY(QStringList logPaths READ logPaths NOTIFY availabilityChanged)

public:
    explicit SecRandomBridge(QObject *parent = nullptr);

    bool available() const { return m_available; }
    QString version() const { return m_version; }
    QStringList logPaths() const { return m_logPaths; }

    // QML 契约：探测安装（注册表 + 盘符扫描 + 版本判定），完成即更新 available/version/logPaths。
    Q_INVOKABLE void probe();
    // 只读监听开关：1500ms 快照基线轮询（本桩仅启停计时器，不解析/不写入）。
    Q_INVOKABLE void setWatchEnabled(bool enabled);

signals:
    void availabilityChanged();
    // 只读命中通知（预留给二期完整实现；本桩不发射历史播报）
    void externalDrawDetected(const QString &info);

private:
    void setAvailable(bool available, const QString &version, const QStringList &paths);

    bool m_available = false;
    QString m_version;
    QStringList m_logPaths;
    bool m_watchEnabled = false;
};
