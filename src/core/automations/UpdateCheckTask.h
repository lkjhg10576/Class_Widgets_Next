#pragma once

#include "AutomationManager.h"

#include <QMetaObject>
#include <QTimer>

// 对应上游 src/core/automations/update_check.py UpdateCheckTask（51 行）。
// 定时自动检查更新：启动 1 秒后首查，之后每 30 分钟一次；
// 检查动作复用 updater 域的 UpdaterBridge::checkUpdate()（update_check.py:34）。
class UpdateCheckTask : public AutomationTask
{
    Q_OBJECT

public:
    explicit UpdateCheckTask(AutomationContext context, QObject *parent = nullptr);

    // base.py:17-19：本任务的调度由私有 QTimer 驱动（update_check.py:24-27），
    // 每秒 tick 无需动作（上游同样未覆盖 update()，继承基类的 pass）
    void update() override {}
    // base.py:21-23
    QString name() const override { return QStringLiteral("UpdateCheckTask"); }

private slots:
    void checkUpdate(); // update_check.py:29-34 _check_update
    // update_check.py:36-51 _handle_update_available
    void handleUpdateAvailable(const QString &version, const QString &url);

private:
    // update_check.py:15 INTERVAL_MS = 30 * 60 * 1000（30min 检查）
    static constexpr int kIntervalMs = 30 * 60 * 1000;

    QTimer m_timer;
    QMetaObject::Connection m_updateAvailableConnection; // update_check.py:33 的连接句柄
    bool m_updateAvailableConnected = false;
};
