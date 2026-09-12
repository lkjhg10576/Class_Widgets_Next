#include "ThemeRecovery.h"

#include "CWThemeManager.h"
#include "Logger.h"

#include <QTimer>

ThemeRecovery::ThemeRecovery(CWThemeManager *themeManager, QObject *parent)
    : QObject(parent)
    , m_themeManager(themeManager)
{
}

void ThemeRecovery::handleFailure(const QString &failedThemeId)
{
    if (!m_themeManager)
        return;

    // theme_recovery.py:22-26：过期的 Loader 错误可能在默认主题已被选中后才到达
    if (m_themeManager->currentTheme() == CWThemeManager::defaultThemeId()
        && failedThemeId != CWThemeManager::defaultThemeId()) {
        return;
    }
    if (m_handling)
        return;

    m_handling = true;
    cwn::Log::error(
        QStringLiteral("Theme '%1' failed to load; restoring default theme").arg(failedThemeId));

    const bool recovered = m_themeManager->rollbackToDefault(failedThemeId);
    if (!recovered)
        cwn::Log::error(QStringLiteral("Unable to recover from theme load failure"));

    // theme_recovery.py:39-44：弹窗只排程一次；defer 到下一轮事件循环，
    // 避免在组件失败的调用栈里同步弹窗
    if (!m_dialogScheduled) {
        m_dialogScheduled = true;
        QTimer::singleShot(0, this, [this, failedThemeId, recovered] {
            showErrorDialog(failedThemeId, recovered);
        });
    }
}

void ThemeRecovery::reportComponentFailure(const QString &source)
{
    if (!m_themeManager)
        return;

    const QString failedThemeId = m_themeManager->currentTheme();
    const QString detail = source.isEmpty() ? QString() : QStringLiteral(": %1").arg(source);
    cwn::Log::error(QStringLiteral("Theme component failed to load for theme '%1'%2")
                        .arg(failedThemeId, detail));

    if (m_handling)
        return;
    // theme_recovery.py:56：defer 到下一轮事件循环再进入恢复流程
    QTimer::singleShot(0, this, [this, failedThemeId] { handleFailure(failedThemeId); });
}

void ThemeRecovery::showErrorDialog(const QString &failedThemeId, bool recovered)
{
    m_dialogScheduled = false;
    emit errorDialogRequested(failedThemeId, recovered);
    m_handling = false;
}
