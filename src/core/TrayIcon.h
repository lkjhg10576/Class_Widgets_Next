#pragma once

#include <QObject>
#include <QPoint>
#include <QSystemTrayIcon>

class QMenu;

// 对应上游 core/utils/tray.py 的 TrayIcon（T11）。
// 图标用 assets/images/tray_icon.png；点击（任意激活方式）发 togglePanel(pos)，
// MainInterface.qml 的 Connections 收到后 raise TrayPanel。
class TrayIcon : public QObject
{
    Q_OBJECT
public:
    explicit TrayIcon(QObject *parent = nullptr);

    bool isValid() const { return m_tray != nullptr; }
    void cleanup();

    void showEditNotification(const QString &title, const QString &text);

signals:
    void togglePanel(const QPoint &pos);
    void editModeRequested();

private:
    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_menu = nullptr;
};
