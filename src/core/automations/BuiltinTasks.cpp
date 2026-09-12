#include "BuiltinTasks.h"

#include "../ConfigStore.h"
#include "../Logger.h"

#include <QStringList>
#include <utility>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace cwn::Log;

namespace {

// schedule/model.py:8-13 EntryType（C++ 侧 ScheduleModel.h 同名常量，本域独立持有）
constexpr char kTypeClass[] = "class";      // EntryType.CLASS
constexpr char kTypeActivity[] = "activity"; // EntryType.ACTIVITY

// builtin_tasks.py:23-27 SYSTEM_WINDOW_CLASSES（仅 Windows 下使用）
const QStringList &systemWindowClasses()
{
    // 每秒可能被 EnumWindows 回调调用数百次，缓存列表避免重复构造
    static const QStringList kClasses = {
        QStringLiteral("Progman"),                     // 桌面
        QStringLiteral("Shell_TrayWnd"),               // 任务栏
        QStringLiteral("Windows.UI.Core.CoreWindow")}; // 输入体验等
    return kClasses;
}

bool readConfigBool(const ConfigStore *configs, const char *key)
{
    if (!configs) {
        return false;
    }
    const std::optional<QJsonValue> v = configs->value(QString::fromLatin1(key));
    return v.has_value() && v->toBool();
}

#ifdef Q_OS_WIN

// builtin_tasks.py:29-31 is_window_maximized：
// GetWindowPlacement 的 showCmd == SW_MAXIMIZE
bool isWindowMaximized(HWND hwnd)
{
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(WINDOWPLACEMENT);
    if (!GetWindowPlacement(hwnd, &placement)) {
        return false;
    }
    return placement.showCmd == SW_MAXIMIZE;
}

// builtin_tasks.py:33-46 is_window_fullscreen
bool isWindowFullscreen(HWND hwnd)
{
    if (!IsWindowVisible(hwnd)) {
        return false; // builtin_tasks.py:34-35
    }
    // builtin_tasks.py:36-39：标准最大化的窗口与显示器边界可能重合，
    // 但无边框全屏应用也可能报告 SW_MAXIMIZE —— 先看是否有标准边框
    const LONG style = GetWindowLongW(hwnd, GWL_STYLE); // builtin_tasks.py:38
    const bool hasStandardFrame = (style & (WS_CAPTION | WS_THICKFRAME)) != 0;
    if (isWindowMaximized(hwnd) && hasStandardFrame) {
        return false; // builtin_tasks.py:40-41
    }
    RECT rect{};
    if (!GetWindowRect(hwnd, &rect)) {
        return false;
    }
    const int screenWidth = GetSystemMetrics(SM_CXSCREEN);  // builtin_tasks.py:43
    const int screenHeight = GetSystemMetrics(SM_CYSCREEN); // builtin_tasks.py:44
    constexpr int margin = 2;                               // builtin_tasks.py:45
    // builtin_tasks.py:46
    return rect.left <= margin && rect.top <= margin
        && rect.right >= screenWidth - margin
        && rect.bottom >= screenHeight - margin;
}

// EnumWindows 回调载体（builtin_tasks.py:94 EnumWindows(self._enum_windows_callback, None)）
struct EnumWindowsContext
{
    AutoHideTask *task = nullptr;
};

// builtin_tasks.py:124-141 _enum_windows_callback
BOOL CALLBACK enumWindowsCallback(HWND hwnd, LPARAM lParam)
{
    auto *ctx = reinterpret_cast<EnumWindowsContext *>(lParam);
    ctx->task->recordWindowState(reinterpret_cast<quint64>(hwnd));
    return TRUE; // builtin_tasks.py:141 return True（继续枚举）
}

#endif // Q_OS_WIN

} // namespace

AutoHideTask::AutoHideTask(AutomationContext context, QObject *parent)
    : AutomationTask(context, parent)
{
#ifdef Q_OS_WIN
    // builtin_tasks.py:53-54：self.runtime.currentsChanged.connect(self.on_schedule_changed)
    // 本移植经 AutomationManager 转发（主控接线见 AutomationManager::onScheduleStatusChanged）
    if (m_ctx.manager) {
        connect(m_ctx.manager, &AutomationManager::scheduleStatusChanged,
                this, &AutoHideTask::onScheduleChanged);
    }

    // builtin_tasks.py:60-63：启动时按配置做一次初始检查
    // （上游读 runtime.current_status，此处读管理器缓存的状态）
    if (readConfigBool(m_ctx.configs, "interactions.hide.in_class") && m_ctx.manager) {
        onScheduleChanged(m_ctx.manager->lastScheduleStatus());
    }

    // builtin_tasks.py:65：启动时检查一次最大化/全屏状态
    update();
#else
    // 非 Windows 平台无窗口检测能力（上游 IS_WINDOWS 分支整体不编译）
    Log::debug(QStringLiteral("AutoHideTask: window detection unavailable on this platform"));
#endif
}

void AutoHideTask::recordWindowState(quint64 hwnd)
{
#ifdef Q_OS_WIN
    const auto handle = reinterpret_cast<HWND>(static_cast<quintptr>(hwnd));
    // builtin_tasks.py:125-126：不可见窗口跳过
    if (!IsWindowVisible(handle)) {
        return;
    }
    // builtin_tasks.py:128：取窗口类名
    wchar_t className[256];
    if (GetClassNameW(handle, className, 256) <= 0) {
        return;
    }
    // builtin_tasks.py:130-132：排除系统窗口
    if (systemWindowClasses().contains(QString::fromWCharArray(className))) {
        return;
    }
    // builtin_tasks.py:134-138：记录最大化状态（fullscreen 字段上游从不读取，省略）
    try {
        m_windowMaximized.insert(hwnd, isWindowMaximized(handle));
    } catch (...) {
        // builtin_tasks.py:139-140：单个窗口检测失败仅记日志
        Log::debug(QStringLiteral("Check window %1 failed").arg(hwnd));
    }
#else
    Q_UNUSED(hwnd);
#endif
}

void AutoHideTask::hide(bool state)
{
    // builtin_tasks.py:67-75 _hide：按配置的隐藏行为写对应字段。
    // 上游直接给模型字段赋值（pydantic validate_assignment），不走 set() 锁检查；
    // 本移植对应 ConfigStore::setInternal（不检查锁定 + dataChanged 通知 QML）
    const QString action = [&] {
        if (!m_ctx.configs) {
            return QStringLiteral("hide");
        }
        const std::optional<QJsonValue> v = m_ctx.configs->value(QStringLiteral("interactions.hide.action"));
        return v.has_value() ? v->toString() : QStringLiteral("hide");
    }();

    if (action == QLatin1String("mini_mode")) {
        // builtin_tasks.py:70-72：TapAction.MINI_MODE（model.py:100）→ preferences.mini_mode
        if (m_ctx.configs) {
            m_ctx.configs->setInternal(QStringLiteral("preferences.mini_mode"), state);
        }
    } else if (action == QLatin1String("hide") || action == QLatin1String("floating_widget")) {
        // builtin_tasks.py:73-75：TapAction.HIDE / FLOATING_WIDGET（model.py:99/101）
        // → interactions.hide.state
        if (m_ctx.configs) {
            m_ctx.configs->setInternal(QStringLiteral("interactions.hide.state"), state);
        }
    }
}

bool AutoHideTask::configBool(const char *key) const
{
    return readConfigBool(m_ctx.configs, key);
}

void AutoHideTask::update()
{
#ifdef Q_OS_WIN
    // builtin_tasks.py:77-122 update() 主循环
    const bool hideMaximized = configBool("interactions.hide.maximized");
    const bool hideFullscreen = configBool("interactions.hide.fullscreen");
    if (!hideMaximized && !hideFullscreen) {
        return; // builtin_tasks.py:79-81：两项都未开启则不处理
    }

    // builtin_tasks.py:83-86：课堂/活动内隐藏优先（in_class 开启时上课不因窗口状态显示）
    const QString status = m_ctx.manager ? m_ctx.manager->lastScheduleStatus()
                                         : QStringLiteral("free");
    if (configBool("interactions.hide.in_class")
        && (status == QLatin1String(kTypeClass) || status == QLatin1String(kTypeActivity))) {
        return;
    }

    bool anyMaximized = false;
    bool anyFullscreen = false;

    // builtin_tasks.py:91-97：最大化检测是全局的，遍历全部可见顶层窗口
    if (hideMaximized) {
        m_windowMaximized.clear(); // builtin_tasks.py:93
        EnumWindowsContext ctx{this};
        EnumWindows(enumWindowsCallback, reinterpret_cast<LPARAM>(&ctx));
        // builtin_tasks.py:96-97（原代码两个重复 if，语义如此合并）
        for (bool maximized : std::as_const(m_windowMaximized)) {
            anyMaximized = anyMaximized || maximized;
        }
    }

    if (hideFullscreen) {
        // builtin_tasks.py:99-116：全屏检测只看前台窗口，且沿用"已跟踪窗口离开
        // 全屏才算退出"的状态机（焦点切走不解除隐藏）
        const HWND foregroundWindow = GetForegroundWindow();
        if (foregroundWindow) {
            wchar_t className[256];
            const bool classNameOk = GetClassNameW(foregroundWindow, className, 256) > 0;
            // builtin_tasks.py:100-105：非系统窗口且全屏 → 记为待跟踪窗口
            if (classNameOk
                && !systemWindowClasses().contains(QString::fromWCharArray(className))
                && isWindowFullscreen(foregroundWindow)) {
                m_fullscreenWindow = reinterpret_cast<quint64>(foregroundWindow);
            }

            // builtin_tasks.py:107-114
            if (m_fullscreenWindow != 0) {
                const auto tracked = reinterpret_cast<HWND>(static_cast<quintptr>(m_fullscreenWindow));
                if (isWindowFullscreen(tracked)) {
                    anyFullscreen = true; // builtin_tasks.py:111-112
                } else {
                    m_fullscreenWindow = 0; // builtin_tasks.py:113-114：窗口退出全屏或已消失
                }
            }
        }
    }

    // builtin_tasks.py:118-122：状态变化时才写配置（避免每秒重复写盘）
    const bool newState = anyMaximized || anyFullscreen;
    if (newState != m_previousState) {
        hide(newState);
    }
    m_previousState = newState;
#else
    // 非 Windows 平台：无窗口检测（与上游 IS_WINDOWS=False 分支一致，任务为空操作）
#endif
}

void AutoHideTask::onScheduleChanged(const QString &currentType)
{
    // builtin_tasks.py:143-148 on_schedule_changed：课程状态变化触发
    if (!configBool("interactions.hide.in_class")) {
        return; // builtin_tasks.py:145-146：未开启"上课时隐藏"则不处理
    }
    // builtin_tasks.py:148：上课（class）或活动（activity）时隐藏，其余显示
    hide(currentType == QLatin1String(kTypeClass) || currentType == QLatin1String(kTypeActivity));
}
