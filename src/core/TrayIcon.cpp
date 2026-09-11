#include "TrayIcon.h"

#include "AppPaths.h"
#include "Logger.h"

#include <QAction>
#include <QCoreApplication>
#include <QCursor>
#include <QIcon>
#include <QMenu>

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

    auto *editModeAction = new QAction(tr("Toggle Edit Mode"), menu);
    connect(editModeAction, &QAction::triggered, this, &TrayIcon::editModeRequested);
    menu->addAction(editModeAction);

    menu->addSeparator();

    auto *quitAction = new QAction(tr("Quit"), menu);
    connect(quitAction, &QAction::triggered, this, [] {
        cwn::Log::info(QStringLiteral("Quit requested from tray"));
        QCoreApplication::quit();
    });
    menu->addAction(quitAction);

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
