#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

class ConfigStore;
class UpdaterBridge;

// 对应上游 src/core/automations/base.py AutomationTask（26 行）。
// 上游任务构造时注入 AppCentral；本移植不改 AppCentral（M4 规则），改用
// 轻量上下文 struct 注入任务实际需要的依赖。
struct AutomationContext
{
    ConfigStore *configs = nullptr;          // 上游 app_central.configs
    UpdaterBridge *updaterBridge = nullptr;  // update_check.py:33-34 经 app_central.updater_bridge 消费
    // 课表事件枢纽：上游 builtin_tasks.py:54 直接连 runtime.currentsChanged；
    // 本移植由主控把 ScheduleRuntime::currentsChanged(QString) 接到
    // AutomationManager::onScheduleStatusChanged，再经 scheduleStatusChanged
    // 信号转发给任务，任务侧不依赖 ScheduleRuntime 头文件
    class AutomationManager *manager = nullptr;
};

class AutomationTask : public QObject
{
    Q_OBJECT

public:
    // base.py:12-15 __init__：enabled 默认 true
    AutomationTask(AutomationContext context, QObject *parent = nullptr);
    ~AutomationTask() override = default;

    // base.py:17-19 update()：每秒调用一次，由 AutomationManager 调度
    virtual void update() = 0;
    // base.py:21-23 name 属性：上游取类名，本移植由各任务返回固定字符串
    virtual QString name() const = 0;

    bool enabled() const { return m_enabled; }               // base.py:14
    void setEnabled(bool enabled) { m_enabled = enabled; }   // 供插件 API 层后续使用

signals:
    // 上游任务直接调用 app_central.tray_icon.push_*（如 update_check.py:44-47）；
    // 本移植经本信号上抛，由 AutomationManager 转发（taskNotification），
    // 主控接到托盘/通知系统
    void notificationRequested(const QString &title, const QString &text);

protected:
    AutomationContext m_ctx;

private:
    bool m_enabled = true;
};

// 对应上游 src/core/automations/manager.py AutomationManager（63 行）。
class AutomationManager : public QObject
{
    Q_OBJECT

public:
    // manager.py:19-23 __init__：上游构造时不注册任务（第 23 行被注释），
    // 由 _run_utils 显式调用 init_builtin_tasks（central.py:464）——本移植保持一致
    explicit AutomationManager(ConfigStore *configs, QObject *parent = nullptr);
    ~AutomationManager() override;

    // 注入 UpdateCheckTask 依赖（对应上游任务读 app_central.updater_bridge）
    void setUpdaterBridge(UpdaterBridge *bridge);

    // manager.py:25-34 init_builtin_tasks：实例化并注册全部内置任务。
    // 上游注册表 = AutoHideTask + UpdateCheckTask + PlazaUpdateCheckTask（manager.py:12、29）；
    // PlazaUpdateCheckTask 依赖 PluginManager（插件广场，Phase 2），本移植不注册。
    void initBuiltinTasks();

    // manager.py:36-45 add_task：注册任务实例，管理器取得所有权（同名覆盖并删除旧实例）
    void addTask(AutomationTask *task);
    // manager.py:47-51 remove_task
    void removeTask(const QString &name);

    // 最近一次课表状态（model.py EntryType.value："class"/"break"/"activity"/
    // "free"/"preparation"）。AutoHideTask 构造时读它做初始检查
    // （上游 builtin_tasks.py:61-63 直接读 runtime.current_status）。
    // 初值 "free"，主控应在 initBuiltinTasks() 前后推送一次当前状态（见报告集成说明）
    QString lastScheduleStatus() const { return m_lastScheduleStatus; }

    QStringList taskNames() const;

public slots:
    // manager.py:53-62 update()：更新全部活动任务。
    // 驱动源 = UnionTimer::instance().tick（central.py:457
    // union_update_timer.tick.connect(self.automation_manager.update)）
    void update();

    // 课表状态变化注入点（主控接线）：
    //   ScheduleRuntime::currentsChanged(QString) → 本槽
    // 行为等价于上游任务各自 connect runtime.currentsChanged：存储最新值并转发
    void onScheduleStatusChanged(const QString &status);

signals:
    void updated(); // manager.py:17 updated 信号（每次 update 后发出）

    // 课表状态变化转发通道（AutoHideTask 在构造时连接本信号）
    void scheduleStatusChanged(const QString &status);

    // 任务通知汇聚出口（AutomationTask::notificationRequested 转发），
    // 主控接到托盘/通知系统
    void taskNotification(const QString &title, const QString &text);

private:
    AutomationContext context();

    ConfigStore *m_configs = nullptr;
    UpdaterBridge *m_updaterBridge = nullptr;
    QHash<QString, AutomationTask *> m_tasks; // manager.py:22 tasks 字典（按 name 索引）
    QString m_lastScheduleStatus = QStringLiteral("free");
};
