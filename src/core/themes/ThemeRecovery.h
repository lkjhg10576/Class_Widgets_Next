#pragma once

#include <QObject>
#include <QString>

class CWThemeManager;

// 对应上游 src/core/theme_recovery.py 的 ThemeRecoveryController。
// 处理异步 QML 主题加载失败：
//   1. handleFailure ← WidgetsWindow::themeLoadFailed（central.py:205 的连接）
//   2. reportComponentFailure ← AppCentral::reportThemeLoadFailure
//      （central.py:282-284 委托入口；WidgetLoader.qml:35 的 QML 错误链路）
// 恢复动作 = CWThemeManager::rollbackToDefault（manager.py:133-161 rollback_to_default）。
// 恢复完成后的错误弹窗：上游调 WindowManager.open_theme_load_error；C++ 侧
// WindowManagerStub 尚无该接口，故以 errorDialogRequested 信号解耦，由 AppCentral
// 决定弹窗承载方式（如 ThemeLoadErrorDialog 对象），接线方式见集成说明。
class ThemeRecovery : public QObject
{
    Q_OBJECT
public:
    explicit ThemeRecovery(CWThemeManager *themeManager, QObject *parent = nullptr);

public slots:
    // theme_recovery.py:20-45 handle_failure
    void handleFailure(const QString &failedThemeId);

    // theme_recovery.py:47-56 report_component_failure
    void reportComponentFailure(const QString &source = QString());

signals:
    // 对应 upstream window_manager.open_theme_load_error(failed_theme_id, recovered)
    void errorDialogRequested(const QString &failedThemeId, bool recovered);

private:
    // theme_recovery.py:58-61 _show_error_dialog
    void showErrorDialog(const QString &failedThemeId, bool recovered);

    CWThemeManager *m_themeManager = nullptr;
    bool m_handling = false;
    bool m_dialogScheduled = false;
};
