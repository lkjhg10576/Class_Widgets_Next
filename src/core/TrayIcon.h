#pragma once

#include <QtCore/qt_windows.h> // HWND/HICON/HBITMAP 等 Windows 类型（自带 NOMINMAX）
#include <QObject>

// 对应上游 core/utils/tray.py 的 TrayIcon（T11）。
//
// B4 后续（用户反馈）：托盘面板（TrayPanel）整体移除——托盘不再唤起任何 QML
// 窗口，左键/中键/双击/右键统一弹原生菜单；"退出"经 quitRequested 信号交
// AppCentral::quit()（exit(0)，绕过 Qt 6.8+ quit() 的窗口关闭协商）。
//
// M4 扩展：托盘菜单项对应 central.py 的窗口管理入口 / 编辑模式 / 迷你模式
// 语义聚合，文本经 "TrayIcon" 翻译上下文：
//   Open Settings / Schedule Editor / Class Swap / Mini Mode / Toggle Edit Mode
//   / Tutorial / Quit，各自通过同名 *Requested 信号交由主控连接
//   （main.cpp / AppCentral 不在本任务改动范围内，现有 connect 保持不变）。
// retranslate() 供语言切换后刷新菜单（连接 AppCentral::retranslate）。
//
// B4（内存优化）：不再使用 QSystemTrayIcon/QMenu（Qt6::Widgets），改用 win32
// Shell_NotifyIcon + 原生 HMENU 弹出菜单，使进程可以只链接 Qt6::Gui。
//   - showMessage → NIF_INFO 气泡（旧 QSystemTrayIcon::Information 语义）
//   - explorer 重启（TaskbarCreated）后自动重新添加图标
class TrayIcon : public QObject
{
    Q_OBJECT
public:
    explicit TrayIcon(QObject *parent = nullptr);
    ~TrayIcon() override;

    bool isValid() const { return m_added; }
    void cleanup();

    void showEditNotification(const QString &title, const QString &text);

    // 语言切换后刷新菜单文本（对照 central.py:443-447 的 retranslate 链）。
    // 原生菜单改为每次右键时按当前语言重建，此处无需做事（保留接口兼容）。
    void retranslate();

signals:
    // ── 既有信号（main.cpp / AppCentral 的连接不能破坏）──
    void editModeRequested();
    // ── M4：菜单动作信号 ──
    void openSettingsRequested();     // 打开设置（对应 window_manager.open_settings）
    void openEditorRequested();       // 课表编辑器（open_editor）
    void openClassSwapRequested();    // 换课（open_class_swap）
    void miniModeRequested();         // 迷你模式切换（model.py:100 MINI_MODE）
    void openTutorialRequested();     // 新手教程（open_tutorial）
    // ── B4 托盘菜单扩展（用户反馈：调休/切换课程表/重启入口，砍"关于"）──
    void rescheduleDayRequested();    // 调休（com.classwidgets.reschedule-day 同路径）
    void switchScheduleRequested();   // 切换课程表（MainInterface 弹 SwitchScheduleDialog）
    void restartRequested();          // 重启（AppCentral::restart 自启新实例）
    void quitRequested();             // 退出（AppCentral::quit：exit(0) 绕过关闭协商）

private:
    // 菜单命令 ID（TrackPopupMenu(TPM_RETURNCMD) 返回值 → 信号映射）
    enum MenuCommand
    {
        CmdOpenSettings = 1,
        CmdOpenEditor,
        CmdRescheduleDay,
        CmdOpenClassSwap,
        CmdSwitchSchedule,
        CmdMiniMode,
        CmdEditMode,
        CmdTutorial,
        CmdRestart,
        CmdQuit,
    };

    static LRESULT CALLBACK trayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    bool registerWindowClass();
    bool addIcon();
    void showContextMenu();
    HICON loadTrayHIcon();
    HBITMAP menuIconBitmap(const QString &assetPath);

    HWND m_hwnd = nullptr;
    HICON m_icon = nullptr;
    UINT m_taskbarCreatedMsg = 0;
    bool m_added = false;

    // 菜单项图标（构造时栅格化一次，cleanup 时销毁；PNG/SVG 经 QIcon 引擎）
    HBITMAP m_bmpSettings = nullptr;
    HBITMAP m_bmpEditor = nullptr;
    HBITMAP m_bmpTutorial = nullptr;
};
