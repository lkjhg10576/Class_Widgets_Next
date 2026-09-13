#pragma once

#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QDateTime>

class ConfigStore;
class ScheduleManager;

// 对应上游 core/schedule/runtime.py ScheduleRuntime（497 行）+
// core/schedule/service.py ScheduleServices（210 行，作为本类内部的
// 纯函数实现，见 ScheduleRuntime.cpp 匿名命名空间）。
//
// 当前状态机：按秒推进（依赖 UnionTimer::tick）计算"上课/课间/活动/预备/休息"
// 状态、剩余时间、下一节等，全部属性以 updated 为 NOTIFY。
// 上游 302-497 行的通知分发依赖 NotificationProvider 体系（M4 落地），
// 本版本保留状态变化检测并记录日志，具体通知由 AppCentral 侧接线补齐。
class ScheduleRuntime : public QObject
{
    Q_OBJECT
    // 属性名与 runtime.py 的 @Property 逐字一致。A2（内存优化）：NOTIFY 按属性
    // 拆分 + 值变化检测 —— 值未变的属性不发信号，QML 绑定不再每秒全量重求值
    // （原先 16 个属性共用 updated，每秒信号风暴驱动全部存活绑定重算）。
    // updated 保留为"任一属性实际变化"的聚合信号，兼容既有 C++/QML 监听方。
    Q_PROPERTY(QString currentTime READ currentTime NOTIFY currentTimeChanged)             // runtime.py:141
    Q_PROPERTY(int currentDayOfWeek READ currentDayOfWeek NOTIFY currentDayOfWeekChanged)  // runtime.py:145
    Q_PROPERTY(QVariantMap currentDate READ currentDate NOTIFY currentDateChanged)        // runtime.py:149
    Q_PROPERTY(int currentWeek READ currentWeek NOTIFY currentWeekChanged)                // runtime.py:153
    Q_PROPERTY(int currentWeekOfCycle READ currentWeekOfCycle NOTIFY currentWeekOfCycleChanged) // runtime.py:157
    Q_PROPERTY(QVariantList subjects READ subjects NOTIFY subjectsChanged)             // runtime.py:162
    Q_PROPERTY(QVariantMap scheduleMeta READ scheduleMeta NOTIFY scheduleMetaChanged)      // runtime.py:168
    Q_PROPERTY(QVariantList currentDayEntries READ currentDayEntries NOTIFY currentDayEntriesChanged) // runtime.py:174
    Q_PROPERTY(QVariantMap currentEntry READ currentEntry NOTIFY currentEntryChanged)      // runtime.py:180
    Q_PROPERTY(QVariantList nextEntries READ nextEntries NOTIFY nextEntriesChanged)       // runtime.py:184
    Q_PROPERTY(int timeOffset READ timeOffset NOTIFY timeOffsetChanged)                  // runtime.py:190
    Q_PROPERTY(QVariantMap remainingTime READ remainingTime NOTIFY remainingTimeChanged)    // runtime.py:194
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)                   // runtime.py:207
    Q_PROPERTY(QString currentStatus READ currentStatus NOTIFY currentStatusChanged)        // runtime.py:213
    Q_PROPERTY(QVariantMap currentSubject READ currentSubject NOTIFY currentSubjectChanged)  // runtime.py:220
    Q_PROPERTY(QString currentTitle READ currentTitle NOTIFY currentTitleChanged)          // runtime.py:224

public:
    // 对应 runtime.py:21 __init__(app_central)：配置与课表管理器均注入。
    // 构造内部完成与 ScheduleManager::scheduleModified、UnionTimer::tick 的
    // 连接（对应 central.py:456/458 的装配）。
    explicit ScheduleRuntime(ConfigStore *configs, ScheduleManager *manager,
                             QObject *parent = nullptr);

    // ── QML 契约（属性读取器）─────────────────────────────────
    // A2：subjects/scheduleMeta/currentDayEntries/currentEntry/currentSubject 的
    // JSON→QVariant 转换结果缓存在成员里，仅在源数据（课表/当天日程/当前条目）
    // 变化时重建，getter 返回隐式共享副本（零深拷贝）
    QString currentTime() const { return m_currentTime.toString(QStringLiteral("HH:mm:ss")); }
    int currentDayOfWeek() const { return m_currentDayOfWeek; }
    QVariantMap currentDate() const;
    int currentWeek() const { return m_currentWeek; }
    int currentWeekOfCycle() const { return m_currentWeekOfCycle; }
    QVariantList subjects() const { return m_subjectsValue; }
    QVariantMap scheduleMeta() const { return m_scheduleMetaValue; }
    QVariantList currentDayEntries() const { return m_currentDayEntriesValue; }
    QVariantMap currentEntry() const { return m_currentEntryValue; }
    QVariantList nextEntries() const { return m_nextEntries; }
    int timeOffset() const { return m_timeOffset; }
    QVariantMap remainingTime() const;
    double progress() const { return m_progress; }
    QString currentStatus() const;
    QVariantMap currentSubject() const { return m_currentSubjectValue; }
    QString currentTitle() const { return m_currentTitle; }

public slots:
    // runtime.py:228-237 refresh(schedule=None)：无参形式重算当前课表
    void refresh();
    // runtime.py:228-237 refresh 的带课表形式（首次装配/强制刷新用）
    void refreshWith(const QJsonObject &schedule);
    // runtime.py:239-242 schedule_refresh：合并连续的课表编辑通知（50ms 去抖）
    void scheduleRefresh(const QJsonObject &schedule);

signals:
    void updated();                    // runtime.py:18（文件/数据更新；A2 起仅任一属性实际变化时发出）
    void currentsChanged(const QString &status); // runtime.py:19 currentsChanged(EntryType)

    // A2：逐属性 NOTIFY 信号（与上方 Q_PROPERTY 一一对应）
    void currentTimeChanged();
    void currentDayOfWeekChanged();
    void currentDateChanged();
    void currentWeekChanged();
    void currentWeekOfCycleChanged();
    void subjectsChanged();
    void scheduleMetaChanged();
    void currentDayEntriesChanged();
    void currentEntryChanged();
    void nextEntriesChanged();
    void timeOffsetChanged();
    void remainingTimeChanged();
    void progressChanged();
    void currentStatusChanged();
    void currentSubjectChanged();
    void currentTitleChanged();

private:
    void recompute(const QJsonObject &schedule, bool scheduleChanged);
    void updateSchedule(const QJsonObject &schedule, bool scheduleChanged); // runtime.py:250-283
    void updateTime();                                // runtime.py:285-288
    double progressPercent() const;                   // runtime.py:290-300
    void updateNotify();                              // runtime.py:302-443（通知简化版）

    // A2：值变化检测赋值 —— 值未变则不写不发（消除每秒信号风暴的主体）
    template <typename T>
    void assignProperty(T &field, const T &newValue, void (ScheduleRuntime::*signal)())
    {
        if (field == newValue)
            return;
        field = newValue;
        m_anyPropertyChanged = true;
        (this->*signal)();
    }

    ConfigStore *m_configs = nullptr;
    ScheduleManager *m_manager = nullptr;

    QTimer m_refreshTimer;           // runtime.py:28-31（50ms 单发，合并编辑通知）
    QJsonObject m_pendingSchedule;

    QJsonObject m_schedule;          // runtime.py:25
    QDateTime m_currentTime;         // runtime.py:32
    QDateTime m_currentOffsetTime;   // runtime.py:33（含 time_offset 的内部计算时间）
    int m_timeOffset = 0;            // runtime.py:38
    int m_currentDayOfWeek = 0;      // runtime.py:35
    int m_currentWeek = 0;           // runtime.py:36
    int m_currentWeekOfCycle = 0;    // runtime.py:37

    QJsonObject m_currentDay;        // runtime.py:41 current_day（Timeline）
    QJsonObject m_previousEntry;     // runtime.py:42 previous_entry（空对象表示 None）
    QJsonObject m_currentEntry;      // runtime.py:43 current_entry
    QVariantList m_nextEntries;      // runtime.py:45（归一化 entry 字典列表）
    qint64 m_remainingSeconds = -1;  // runtime.py:46 remaining_time（秒；-1 表示 None）
    double m_progress = 0.0;         // runtime.py:47
    QString m_currentStatus;         // runtime.py:48（EntryType.value）
    QJsonObject m_currentSubject;    // runtime.py:50
    QString m_currentTitle;          // runtime.py:51

    // A2：QVariant 派生缓存（getter 直读；源数据变化时在赋值点重建）
    QVariantList m_subjectsValue;
    QVariantMap m_scheduleMetaValue;
    QVariantList m_currentDayEntriesValue;
    QVariantMap m_currentEntryValue;
    QVariantMap m_currentSubjectValue;
    bool m_anyPropertyChanged = false;
};
