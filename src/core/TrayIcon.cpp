#include "TrayIcon.h"

#include "AppPaths.h"
#include "Logger.h"

#include <QAction>
#include <QCoreApplication>
#include <QCursor>
#include <QIcon>
#include <QMenu>
#include <utility> // std::as_const

TrayIcon::TrayIcon(QObject *parent)
    : QObject(parent)
{
    const QString iconPath = AppPaths::instance().assetsRoot()
        + QStringLiteral("/images/tray_icon.png");
    const QIcon icon(iconPath);
    if (icon.isNull()) {
        cwn::Log::error(QStringLiteral("Tray icon not found: %1").arg(iconPath));
        return;
    }

    auto *menu = new QMenu();
    m_menu = menu;

    // 菜单项（M4 补全；对照任务要求与 central.py 窗口管理入口语义）。
    // tray.py 本体仅承载图标与点击 togglePanel（tray.py:42-47），菜单为 M1
    // 简化版的扩展落地，文本全部走 "TrayIcon" 翻译上下文。
    addMenuAction("Open Settings", assetIcon(QStringLiteral("cw2_settings.png")),
                  &TrayIcon::openSettingsRequested);
    addMenuAction("Schedule Editor", assetIcon(QStringLiteral("cw2_editor.png")),
                  &TrayIcon::openEditorRequested);
    addMenuAction("Class Swap", QIcon(), &TrayIcon::openClassSwapRequested);

    menu->addSeparator();

    addMenuAction("Mini Mode", QIcon(), &TrayIcon::miniModeRequested);
    addMenuAction("Toggle Edit Mode", QIcon(), &TrayIcon::editModeRequested);

    menu->addSeparator();

    addMenuAction("Tutorial", assetIcon(QStringLiteral("smart_teach.svg")),
                  &TrayIcon::openTutorialRequested);
    addMenuAction("About", assetIcon(QStringLiteral("cw2_info.png")),
                  &TrayIcon::openAboutRequested);

    menu->addSeparator();

    // 退出（保持 M1 行为：直接退出常驻应用）
    auto *quitAction = new QAction(QCoreApplication::translate("TrayIcon", "Quit"), menu);
    connect(quitAction, &QAction::triggered, this, [] {
        cwn::Log::info(QStringLiteral("Quit requested from tray"));
        QCoreApplication::quit();
    });
    menu->addAction(quitAction);
    m_menuEntries.append({ quitAction, "Quit" });

    m_tray = new QSystemTrayIcon(icon, this);
    m_tray->setToolTip(QStringLiteral("Class Widgets Next"));
    m_tray->setContextMenu(menu);
    connect(m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                // 对应 tray.py on_click：任意激活方式都发 togglePanel
                Q_UNUSED(reason);
                emit togglePanel(QCursor::pos());
            });
    m_tray->show();
    cwn::Log::info(QStringLiteral("Tray icon initialized"));
}

void TrayIcon::cleanup()
{
    m_menuEntries.clear();
    if (m_menu) {
        m_menu->deleteLater();
        m_menu = nullptr;
    }
    if (!m_tray)
        return;
    m_tray->hide();
    m_tray->deleteLater();
    m_tray = nullptr;
}

void TrayIcon::showEditNotification(const QString &title, const QString &text)
{
    if (m_tray)
        m_tray->showMessage(title, text, QSystemTrayIcon::Information, 5000);
}

void TrayIcon::retranslate()
{
    // 语言切换后原地刷新菜单文本（对应 central.py retranslate 链路）
    for (const MenuEntry &entry : std::as_const(m_menuEntries)) {
        entry.action->setText(QCoreApplication::translate("TrayIcon", entry.sourceText));
    }
}

QIcon TrayIcon::assetIcon(const QString &fileName) const
{
    // assets/images/icons/<fileName>（cw2_*.png 与 smart_teach.svg 均在仓库内；
    // 文件缺失时 QIcon 为空，菜单项自动隐藏图标）
    return QIcon(AppPaths::instance().assetsRoot()
                 + QStringLiteral("/images/icons/") + fileName);
}

void TrayIcon::addMenuAction(const char *sourceText, const QIcon &icon,
                             void (TrayIcon::*signalPtr)())
{
    auto *action = new QAction(QCoreApplication::translate("TrayIcon", sourceText), m_menu);
    if (!icon.isNull())
        action->setIcon(icon);
    // 信号 → 信号转发：菜单动作交由主控连接对应处理器（main.cpp / AppCentral）
    connect(action, &QAction::triggered, this, signalPtr);
    m_menu->addAction(action);
    m_menuEntries.append({ action, sourceText });
}
