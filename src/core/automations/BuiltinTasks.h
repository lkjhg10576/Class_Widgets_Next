#pragma once

#include "AutomationManager.h"

#include <QHash>

// 对应上游 src/core/automations/builtin_tasks.py AutoHideTask（148 行）。
// 内置自动化任务：按配置在"窗口最大化 / 全屏 / 上课"时自动隐藏小组件窗口。
//
// 上游用 pywin32（win32gui/win32con/ctypes）做窗口检测；本移植直接调 Win32 API
// （user32，Qt6::Gui 已隐式链接），判定逻辑与 builtin_tasks.py:29-46 逐行对齐。
// 本文件对应 builtin_tasks.py 的全部内置任务（该文件仅定义 AutoHideTask 一个任务；
// 注意 builtin_tasks.py 顶部的 import platform 仅服务于窗口检测，无关机类任务）。
class AutoHideTask : public AutomationTask
{
    Q_OBJECT

public:
    explicit AutoHideTask(AutomationContext context, QObject *parent = nullptr);

    // builtin_tasks.py:77-122 update()：主循环（原每秒由 AutomationManager 调度；
    // A6 起窗口扫描节流至 kScanIntervalMs，EnumWindows 全表重建不再每秒发生）
    void update() override;
    // base.py:21-23
    QString name() const override { return QStringLiteral("AutoHideTask"); }

    // builtin_tasks.py:124-141 _enum_windows_callback 的等价实现：记录单个窗口的
    // 最大化状态（上游同时记录 fullscreen 字段但从不读取，见 97 行仅取 'maximized'）
    void recordWindowState(quint64 hwnd);

private slots:
    // builtin_tasks.py:143-148 on_schedule_changed：课程状态变化触发
    void onScheduleChanged(const QString &currentType);

private:
    // A6：窗口扫描节流间隔（计划 A6 备选"降频至 2–5s"的 3s 档；
    // 上课隐藏路径走 onScheduleChanged 信号，不受节流影响）
    static constexpr qint64 kScanIntervalMs = 3000;

    // builtin_tasks.py:67-75 _hide：按 interactions.hide.action 写入对应配置字段
    void hide(bool state);

    bool configBool(const char *key) const;

    // key = 窗口句柄（HWND 以 quint64 保存，头文件不引入 windows.h）
    QHash<quint64, bool> m_windowMaximized; // builtin_tasks.py:56 _window_states
    quint64 m_fullscreenWindow = 0;         // builtin_tasks.py:58 _fullscreen_window
    bool m_previousState = false;           // builtin_tasks.py:57 previous_state
    qint64 m_lastScanMs = 0;                // A6：上次窗口扫描时刻（0 = 尚未扫描）
};
