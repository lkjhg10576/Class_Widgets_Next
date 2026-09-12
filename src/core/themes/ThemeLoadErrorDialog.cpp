#include "ThemeLoadErrorDialog.h"

#include "../Logger.h"

ThemeLoadErrorDialog::ThemeLoadErrorDialog(QObject *parent)
    : QObject(parent)
{
}

void ThemeLoadErrorDialog::setErrorDetails(const QString &failedThemeId, bool recovered)
{
    // windows.py:230-234：更新错误详情并逐项广播（QML Connections 依赖这两个信号）
    if (m_failedThemeId != failedThemeId) {
        m_failedThemeId = failedThemeId;
        emit failedThemeIdChanged(m_failedThemeId);
    }
    if (m_recovered != recovered) {
        m_recovered = recovered;
        emit recoveredChanged(m_recovered);
    }
    cwn::Log::info(QStringLiteral("Theme load error details set: theme='%1' recovered=%2")
                       .arg(failedThemeId)
                       .arg(recovered ? QStringLiteral("true") : QStringLiteral("false")));
}
