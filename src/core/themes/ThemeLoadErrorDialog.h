#pragma once

#include <QObject>
#include <QString>

// 对应上游 src/core/windows/windows.py:201-258 的 ThemeLoadErrorDialog（QObject 部分）。
// 上游同时持有独立引擎窗口（ReleasableWindow）；C++ 侧窗口仍归 WindowManagerStub，
// 这里先提供 QML 消费的上下文对象本体。
//
// QML 消费面（app/src/qml/ClassWidgets/Components/dialogs/ThemeLoadErrorDialog.qml）：
//   Connections {
//       target: ThemeLoadErrorDialog
//       function onFailedThemeIdChanged(value) { ... }
//       function onRecoveredChanged(value) { ... }
//   }
// 注册方式：engine->rootContext()->setContextProperty("ThemeLoadErrorDialog", obj)
class ThemeLoadErrorDialog : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString failedThemeId READ failedThemeId NOTIFY failedThemeIdChanged)
    Q_PROPERTY(bool recovered READ recovered NOTIFY recoveredChanged)

public:
    explicit ThemeLoadErrorDialog(QObject *parent = nullptr);

    QString failedThemeId() const { return m_failedThemeId; }
    bool recovered() const { return m_recovered; }

    // windows.py:229-234 set_error_details
    Q_INVOKABLE void setErrorDetails(const QString &failedThemeId, bool recovered);

    // windows.py:236-238 show_when_ready：置位后请求显示（窗口创建/显示由持有
    // 引擎的窗口层完成）；windows.py:240-243 的 objectCreated 自动显示在 C++ 侧
    // 等价于连接 showRequested 信号
    void showWhenReady() { m_showWhenReady = true; emit showRequested(); }

signals:
    void failedThemeIdChanged(const QString &failedThemeId);
    void recoveredChanged(bool recovered);
    void showRequested();

private:
    QString m_failedThemeId;
    bool m_recovered = true;
    bool m_showWhenReady = false;
};
