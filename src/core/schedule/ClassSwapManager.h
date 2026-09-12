#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class ConfigStore;
class ScheduleManager;

// 对应上游 core/schedule/swapper.py ClassSwapManager（774 行）。
// 临时换课：通过操作 schedule.overrides 实现当天换课，记录持久化到
// configs.json 的 schedule.class_swap 键；跨天自动清理。
// 注册到 QML 的上下文名为首字母大写的 "ClassSwapManager"（AppCentral.cpp:87）。
class ClassSwapManager : public QObject
{
    Q_OBJECT
public:
    // 对应 swapper.py:32 __init__(app_central)：configs 与 manager 均注入
    explicit ClassSwapManager(ConfigStore *configs, ScheduleManager *manager,
                              QObject *parent = nullptr);

    // ── 数据查询（swapper.py:44-161）──────────────────────────
    // 指定 星期+周次 的当日课程（已应用 override，仅 class/activity）
    Q_INVOKABLE QVariantList getDayEntries(int dayOfWeek, int weekOfCycle);
    Q_INVOKABLE QVariantList getAllSubjects();
    Q_INVOKABLE int getCurrentDayOfWeek();
    Q_INVOKABLE int getCurrentWeekOfCycle();
    // 换课界面上次选择的星期/周期周（默认今天）
    Q_INVOKABLE int getPreferredDayOfWeek();
    Q_INVOKABLE int getPreferredWeekOfCycle();
    // 保存换课界面当前选择的 星期/周期周
    Q_INVOKABLE void setSwapPickerContext(int dayOfWeek, int weekOfCycle);
    // 将所选 星期/周次 课表立即投射到今天
    Q_INVOKABLE bool applyPickerToToday(int dayOfWeek, int weekOfCycle);
    Q_INVOKABLE int getMaxWeekCycle();
    // 根据 subjectId 获取科目名
    Q_INVOKABLE QString getSubjectName(const QString &subjectId);

    // ── 换课操作（swapper.py:165-291）─────────────────────────
    // 交换两节课的科目
    Q_INVOKABLE bool swapTwoEntries(const QString &entryIdA, const QString &entryIdB,
                                    int dayOfWeek, int weekOfCycle);
    // 将某节课替换为指定科目
    Q_INVOKABLE bool replaceEntry(const QString &entryId, const QString &newSubjectId,
                                  int dayOfWeek, int weekOfCycle);

    // ── 持久化（swapper.py:293-398）───────────────────────────
    Q_INVOKABLE void saveSwapRecords();
    Q_INVOKABLE void loadSwapRecords();
    Q_INVOKABLE bool hasTodaySwaps();
    Q_INVOKABLE QVariantList getSwapRecords();
    // 丢弃今天的换课（撤销所有换课 override）
    Q_INVOKABLE void discardTodaySwaps();

signals:
    void updated();          // swapper.py:29
    void swapCommitted();    // swapper.py:30（换课提交成功）

private:
    // ── 内部方法（swapper.py:400-774）─────────────────────────
    QJsonObject findSubject(const QString &subjectId);
    QVariantMap effectiveSubject(const QString &entryId, int dayOfWeek, int weekOfCycle,
                                 int maxCycle);
    void setOrUpdateOverride(const QString &entryId, const QVariantList &dayOfWeek,
                             const QJsonValue &weeks, const QString &subjectId,
                             const QString &title,
                             const QString &startTime = QString(),
                             const QString &endTime = QString());
    void addSwapRecord(const QString &swapType, const QString &entryA, const QString &entryB,
                       const QString &oldSubject, const QString &newSubject);
    QVariantMap normalizeSwapRecord(const QVariantMap &record);
    QString mapEntryToDay(const QString &sourceEntryId, int sourceDayOfWeek,
                          int sourceWeekOfCycle, int targetDayOfWeek, int targetWeekOfCycle);
    void applyDayScheduleToToday(int sourceDayOfWeek, int sourceWeekOfCycle,
                                 int targetDayOfWeek, int targetWeekOfCycle);
    QVariantList dayEntriesInternal(int dayOfWeek, int weekOfCycle, bool includeNonClass);
    void clearTodaySwapOverrides(int dayOfWeek, int weekOfCycle);
    void cleanupSwapOverrides(const QVariantList &records);
    void rebuildOverridesFromRecords(const QVariantList &records);

    ConfigStore *m_configs = nullptr;
    ScheduleManager *m_manager = nullptr;
    QVariantList m_swapRecords;   // swapper.py:38（用于持久化）
    QString m_swapDate;           // swapper.py:40
};
