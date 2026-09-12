#pragma once

#include <QObject>
#include <QPoint>
#include <QSystemTrayIcon>
#include <QVector>

class QAction;
class QMenu;
class QIcon;

// 对应上游 core/utils/tray.py 的 TrayIcon（T11）。
// 图标用 assets/images/tray_icon.png；点击（任意激活方式）发 togglePanel(pos)，
// MainInterface.qml 的 Connections 收到后 raise TrayPanel（tray.py:42-47 on_click）。
//
// M4 扩展：补全托盘右键菜单（上游 tray.py 本体无 QMenu，菜单项对应
// central.py 的窗口管理入口 / 编辑模式 / 迷你模式语义聚合，文本经 tr()
// 走 "TrayIcon" 翻译上下文）：
//   Open Settings / Schedule Editor / Class Swap / Mini Mode / Toggle Edit Mode
//   / Tutorial / About / Quit，各自通过同名 *Requested 信号交由主控连接
//   （main.cpp / AppCentral 不在本任务改动范围内，现有 connect 保持不变）。
// 保持既有信号 togglePanel(QPoint) / editModeRequested 的签名与语义不变；
// retranslate() 供语言切换后原地刷新菜单文本（连接 AppCentral::retranslate）。
class TrayIcon : public QObject
{
    Q_OBJECT
public:
    explicit TrayIcon(QObject *parent = nullptr);

    bool isValid() const { return m_tray != nullptr; }
    void cleanup();

    void showEditNotification(const QString &title, const QString &text);

    // 语言切换后刷新菜单文本（对照 central.py:443-447 的 retranslate 链）
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
    // 菜单文本 retranslate 用（英文源文 + QAction 对）
    struct MenuEntry
    {
        QAction *action = nullptr;
        const char *sourceText = nullptr;
    };

    QIcon assetIcon(const QString &fileName) const;
    void addMenuAction(const char *sourceText, const QIcon &icon,
                       void (TrayIcon::*signalPtr)());

    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_menu = nullptr;
    QVector<MenuEntry> m_menuEntries;
};
