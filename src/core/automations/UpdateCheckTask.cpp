#include "UpdateCheckTask.h"

#include "../ConfigStore.h"
#include "../Logger.h"
#include "../updater/UpdaterBridge.h"

#include <QCoreApplication>
#include <utility>

using cwn::Log;

namespace {

bool readConfigBool(const ConfigStore *configs, const char *key)
{
    if (!configs) {
        return false;
    }
    const std::optional<QJsonValue> v = configs->value(QString::fromLatin1(key));
    return v.has_value() && v->toBool();
}

} // namespace

UpdateCheckTask::UpdateCheckTask(AutomationContext context, QObject *parent)
    : AutomationTask(context, parent)
{
    // update_check.py:21-22：未开启自动检查则不建定时器
    // （设置项注明 "* Requires restart"，运行期改动不生效，与上游一致）
    if (!readConfigBool(m_ctx.configs, "network.auto_check_updates")) {
        return;
    }

    // update_check.py:24-27：首个 1 秒触发，_check_update 里把周期切到 30 分钟
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &UpdateCheckTask::checkUpdate);
    m_timer.start();
}

void UpdateCheckTask::checkUpdate()
{
    // update_check.py:29-34 _check_update
    if (!enabled()) {
        return; // update_check.py:30-31
    }
    m_timer.setInterval(kIntervalMs); // update_check.py:32：切换到 30min 周期

    // update_check.py:33：连接 updateAvailable（显示一次通知后在 handler 内断开；
    // 此处用连接句柄防重复连接）
    if (m_ctx.updaterBridge && !m_updateAvailableConnected) {
        m_updateAvailableConnected = true;
        m_updateAvailableConnection = connect(m_ctx.updaterBridge, &UpdaterBridge::updateAvailable,
                                              this, &UpdateCheckTask::handleUpdateAvailable);
    }

    // update_check.py:34：app_central.updater_bridge.checkUpdate()
    if (m_ctx.updaterBridge) {
        m_ctx.updaterBridge->checkUpdate();
    }
}

void UpdateCheckTask::handleUpdateAvailable(const QString &version, const QString &url)
{
    // update_check.py:36-51 _handle_update_available
    Log::info(QStringLiteral("Update available: %1, %2").arg(version, url)); // update_check.py:37

    try {
        // update_check.py:39-47：托盘通知（上游 tray_icon.push_update_notification；
        // 本移植经基类通知信号上抛，主控接到托盘/通知系统）
        const QString text = QCoreApplication::translate(
            "UpdateNotification",
            "\"%1\" is available!\nGo to \"Settings\" → \"Update\" for more details.").arg(version);
        emit notificationRequested(
            QCoreApplication::translate("UpdateNotification",
                                        "Class Widgets Update Available"),
            text);
    } catch (...) {
        // update_check.py:50：通知失败仅记日志
        Log::error(QStringLiteral("Failed to show tray message"));
    }

    // update_check.py:48：通知一次后断开，避免重复弹托盘
    if (m_updateAvailableConnected) {
        disconnect(m_updateAvailableConnection);
        m_updateAvailableConnected = false;
    }
}
