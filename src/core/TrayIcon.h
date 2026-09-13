#pragma once

#include <QtCore/qt_windows.h> // HWND/HICON/HBITMAP 等 Windows 类型（自带 NOMINMAX）
#include <QObject>
#include <QPoint>

// 对应上游 core/utils/tray.py 的 TrayIcon（T11）。
// 图标用 assets/images/tray_icon.png；点击（任意激活方式）发 togglePanel(pos)，
// MainInterface.qml 的 Connections 收到后 raise TrayPanel（tray.py:42-47 on_click）。
//
// M4 扩展：补全托盘右键菜单（上游 tray.py 本体无菜单，菜单项对应
// central.py 的窗口管理入口 / 编辑模式 / 迷你模式语义聚合，文本经
// "TrayIcon" 翻译上下文）：
//   Open Settings / Schedule Editor / Class Swap / Mini Mode / Toggle Edit Mode
//   / Tutorial / About / Quit，各自通过同名 *Requested 信号交由主控连接
//   （main.cpp / AppCentral 不在本任务改动范围内，现有 connect 保持不变）。
// 保持既有信号 togglePanel(QPoint) / editModeRequested 的签名与语义不变；
// retranslate() 供语言切换后刷新菜单（连接 AppCentral::retranslate）。
//
// B4（内存优化）：不再使用 QSystemTrayIcon/QMenu（Qt6::Widgets），改用 win32
// Shell_NotifyIcon + 原生 HMENU 弹出菜单，使进程可以只链接 Qt6::Gui。
// 对外接口与旧实现逐位对齐：
//   - 左键/中键/双击 → togglePanel(QCursor::pos())（旧 activated 任意 reason 语义）
//   - 右键 → 仅弹原生菜单（不再发 togglePanel：旧行为会先唤起 TrayPanel 再被
//     菜单抢焦点收回，实测观感为"闪一下大窗口"，用户反馈后移除）
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
    // ── 既有信号（main.cpp:84-86 / AppCentral 的连接不能破坏）──
    void togglePanel(const QPoint &pos);
    void editModeRequested();
    // ── M4 新增：菜单动作信号 ──
    void openSettingsRequested();     // 打开设置（对应 window_manager.open_settings）
    void openEditorRequested();       // 课表编辑器（open_editor）
    void openClassSwapRequested();    // 换课（open_class_swap）
    void miniModeRequested();         // 迷你模式切换（model.py:100 MINI_MODE）
    void openTutorialRequested();     // 新手教程（open_tutorial）
    void openAboutRequested();        // 关于（settings/pages/About.qml）

private:
    // 菜单命令 ID（TrackPopupMenu(TPM_RETURNCMD) 返回值 → 信号映射）
    enum MenuCommand
    {
        CmdOpenSettings = 1,
        CmdOpenEditor,
        CmdOpenClassSwap,
        CmdMiniMode,
        CmdEditMode,
        CmdTutorial,
        CmdAbout,
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

    // 菜单项图标（构造时栅格化一次，cleanup 时销毁；顺序对应 MenuCommand）
    HBITMAP m_bmpSettings = nullptr;
    HBITMAP m_bmpEditor = nullptr;
    HBITMAP m_bmpTutorial = nullptr;
    HBITMAP m_bmpAbout = nullptr;
};
