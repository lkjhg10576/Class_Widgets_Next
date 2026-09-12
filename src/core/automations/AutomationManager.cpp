#include "AutomationManager.h"

#include "BuiltinTasks.h"
#include "UpdateCheckTask.h"
#include "../ConfigStore.h"
#include "../Logger.h"

#include <utility>

using cwn::Log;

AutomationTask::AutomationTask(AutomationContext context, QObject *parent)
    : QObject(parent)
    , m_ctx(context)
{
}

AutomationManager::AutomationManager(ConfigStore *configs, QObject *parent)
    : QObject(parent)
    , m_configs(configs)
{
}

AutomationManager::~AutomationManager()
{
    // 任务均以管理器为 QObject 父对象（addTask 里 setParent），此处无需手动清理
}

void AutomationManager::setUpdaterBridge(UpdaterBridge *bridge)
{
    m_updaterBridge = bridge;
}

AutomationContext AutomationManager::context() const
{
    AutomationContext ctx;
    ctx.configs = m_configs;
    ctx.updaterBridge = m_updaterBridge;
    ctx.manager = this;
    return ctx;
}

void AutomationManager::initBuiltinTasks()
{
    // manager.py:25-34 init_builtin_tasks
    // manager.py:27-31 注册表（PlazaUpdateCheckTask 属插件广场 Phase 2，不移植：
    //   manager.py:12 from .plaza_update_check import PlazaUpdateCheckTask
    //   manager.py:29 PlazaUpdateCheckTask 在 builtin_tasks 列表中的位置在此省略）
    addTask(new AutoHideTask(context(), this));
    addTask(new UpdateCheckTask(context(), this));
}

void AutomationManager::addTask(AutomationTask *task)
{
    // manager.py:36-45 add_task
    if (!task) {
        return;
    }
    task->setParent(this); // C++ 侧所有权语义（上游由 Python GC 管理）

    const QString name = task->name();
    if (m_tasks.contains(name)) {
        // manager.py:43：同名任务覆盖旧实例
        Log::warn(QStringLiteral("Task '%1' already exists, overwriting old instance").arg(name));
        delete m_tasks.take(name);
    }
    m_tasks.insert(name, task);
    // 任务通知汇聚（上游任务直接调 tray_icon，见 AutomationTask 注释）
    connect(task, &AutomationTask::notificationRequested,
            this, &AutomationManager::taskNotification);
    // manager.py:45
    Log::debug(QStringLiteral("Added automation task: %1").arg(name));
}

void AutomationManager::removeTask(const QString &name)
{
    // manager.py:47-51 remove_task
    if (AutomationTask *task = m_tasks.take(name)) {
        disconnect(task, nullptr, this, nullptr);
        task->deleteLater();
        // manager.py:51
        Log::debug(QStringLiteral("Removed automation task: %1").arg(name));
    }
}

QStringList AutomationManager::taskNames() const
{
    QStringList names;
    names.reserve(m_tasks.size());
    for (auto it = m_tasks.constBegin(); it != m_tasks.constEnd(); ++it) {
        names.append(it.key());
    }
    return names;
}

void AutomationManager::update()
{
    // manager.py:53-62 update()：更新全部活动任务（每秒由 UnionTimer::tick 驱动）
    for (AutomationTask *task : std::as_const(m_tasks)) {
        if (!task->enabled()) {
            continue; // manager.py:56-57
        }
        try {
            task->update(); // manager.py:58-59
        } catch (const std::exception &e) {
            // manager.py:60-61：异常按任务隔离，记日志不中断其它任务
            Log::error(QStringLiteral("Error executing task '%1': %2")
                           .arg(task->name(), QString::fromUtf8(e.what())));
        } catch (...) {
            Log::error(QStringLiteral("Error executing task '%1': unknown exception")
                           .arg(task->name()));
        }
    }
    emit updated(); // manager.py:62
}

void AutomationManager::onScheduleStatusChanged(const QString &status)
{
    // 存储最新状态（供后构造的任务做初始检查，builtin_tasks.py:61-63 的替代）并转发
    m_lastScheduleStatus = status;
    emit scheduleStatusChanged(status);
}
