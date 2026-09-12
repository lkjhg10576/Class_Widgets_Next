#pragma once

#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QVariant>
#include <QVariantList>

class ScheduleManager;

// 对应上游 core/schedule/editor.py ScheduleEditor（699 行）。
// 编辑器窗口（QML 21 个编辑器文件）的后端：科目/时间线/条目/Override 的
// 增删改查全部在本对象内存上进行，经 100ms 去抖后由 modify() 提交给
// ScheduleManager（editor.py:149-150 refresh_manager）。
// 数据形状见 ScheduleModel.h（QJsonObject 承载 pydantic model_dump 结果）。
class ScheduleEditor : public QObject
{
    Q_OBJECT
    // 以下属性与 editor.py 的 @Property 逐字一致
    Q_PROPERTY(QVariant meta READ meta NOTIFY metaChanged)                       // editor.py:629
    Q_PROPERTY(QVariantList subjects READ subjects NOTIFY subjectsChanged)       // editor.py:636
    Q_PROPERTY(QVariantList days READ days NOTIFY daysChanged)                   // editor.py:643
    Q_PROPERTY(int entriesRevision READ entriesRevision NOTIFY entriesChanged)   // editor.py:648
    Q_PROPERTY(QVariantList entriesData READ entriesData NOTIFY entriesChanged)  // editor.py:653
    Q_PROPERTY(QVariantList overrides READ overrides NOTIFY overridesChanged)    // editor.py:658
    Q_PROPERTY(int overridesRevision READ overridesRevision
               NOTIFY overridesRevisionChanged)                                  // editor.py:666
    Q_PROPERTY(QVariant scheduleData READ scheduleData NOTIFY updated)           // editor.py:671
    Q_PROPERTY(QString path READ path NOTIFY updated)                            // editor.py:678
    Q_PROPERTY(QString filename READ filename NOTIFY updated)                    // editor.py:683
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)                        // editor.py:696

public:
    // 对应 editor.py:45 __init__(manager)
    explicit ScheduleEditor(ScheduleManager *manager, QObject *parent = nullptr);

    // ── QML 契约（属性读取器）─────────────────────────────────
    QVariant meta() const;
    QVariantList subjects() const;
    QVariantList days() const { return m_daysData; }
    int entriesRevision() const { return m_entriesRevision; }
    QVariantList entriesData() const { return m_entriesData; }
    QVariantList overrides() const;
    int overridesRevision() const { return m_overridesRevision; }
    QVariant scheduleData() const { return m_schedule.toVariantMap(); }
    QString path() const;
    QString filename() const { return m_filename; }
    bool dirty() const { return m_dirty; }

    // ── Subject 操作（editor.py:153-215）───────────────────────
    // editor.py:153 addSubject(name, teacher, icon, color, location, isLocal) → id
    Q_INVOKABLE QString addSubject(const QString &name, const QString &teacher = QString(),
                                   const QString &icon = QString(), const QString &color = QString(),
                                   const QString &location = QString(),
                                   bool isLocalClassroom = true);
    // editor.py:171-194 updateSubject：空串字段语义见实现（name/simplified 保留旧值，
    // 其余清空），与上游 `x or None` 完全一致
    Q_INVOKABLE void updateSubject(const QString &subjectId, const QString &name = QString(),
                                   const QString &simplifiedName = QString(),
                                   const QString &teacher = QString(),
                                   const QString &icon = QString(), const QString &color = QString(),
                                   const QString &location = QString(),
                                   bool isLocalClassroom = true);
    // editor.py:196-210 removeSubject：同时删除相关课程条目
    Q_INVOKABLE void removeSubject(const QString &subjectId);
    // editor.py:212-215 getSubject → 科目字典或 null
    Q_INVOKABLE QVariant getSubject(const QString &subjectId) const;
    // editor.py:451-457 subjectNameById
    Q_INVOKABLE QString subjectNameById(const QString &subjectId) const;
    // editor.py:599-607 restoreDefaultSubjects：加载默认学科
    Q_INVOKABLE void restoreDefaultSubjects();

    // ── Day 操作（editor.py:217-299）───────────────────────────
    // editor.py:218-233 addDay(dayOfWeek, weeks, date) → id；QML 可能传
    // int/list/null（dayOfWeek）、"all"/int/list/null（weeks）、字符串或 JS Date（date）
    Q_INVOKABLE QString addDay(const QVariant &dayOfWeek, const QVariant &weeks,
                               const QVariant &date);
    // editor.py:235-256 updateDay：提交完整模式状态，清除前一模式的残留字段
    Q_INVOKABLE void updateDay(const QString &dayId, const QVariant &dayOfWeek,
                               const QVariant &weeks, const QVariant &date);
    // editor.py:258-270 removeDay
    Q_INVOKABLE void removeDay(const QString &dayId);
    // editor.py:272-294 duplicateDay：深拷贝并为 day/entry 生成新 id
    Q_INVOKABLE QString duplicateDay(const QString &dayId);
    // editor.py:296-299 getDay → 日程字典或 null
    Q_INVOKABLE QVariant getDay(const QString &dayId) const;

    // ── Entry 操作（editor.py:301-386）─────────────────────────
    // editor.py:302-328 addEntry(dayId, type, start, end, subjectId, title) → id
    Q_INVOKABLE QString addEntry(const QString &dayId, const QString &entryType,
                                 const QString &startTime, const QString &endTime,
                                 const QString &subjectId = QString(),
                                 const QString &title = QString());
    // editor.py:330-366 updateEntry：QML 的 subjectId/title 可能传 null（→ 空串语义）
    Q_INVOKABLE void updateEntry(const QString &entryId, const QString &entryType,
                                 const QString &startTime, const QString &endTime,
                                 const QString &subjectId = QString(),
                                 const QString &title = QString());
    // editor.py:368-377 removeEntry
    Q_INVOKABLE void removeEntry(const QString &entryId);
    // editor.py:379-386 getEntry → 条目字典或 null
    Q_INVOKABLE QVariant getEntry(const QString &entryId) const;

    // ── Override 操作（editor.py:388-449）──────────────────────
    // editor.py:389-404 findOverride：查找已有 override，返回 id（无则空串）
    Q_INVOKABLE QString findOverride(const QString &entryId, const QVariant &dayOfWeek,
                                     const QVariant &weeks) const;
    // editor.py:406-422 addOverride
    Q_INVOKABLE bool addOverride(const QString &entryId, const QVariant &dayOfWeek,
                                 const QVariant &weeks, const QString &subjectId = QString(),
                                 const QString &title = QString());
    // editor.py:424-437 updateOverride：参数为 null 时不修改对应字段
    //（QML Schedule.qml:39 传 null 表示保持旧 title）
    Q_INVOKABLE bool updateOverride(const QString &overrideId, const QVariant &subjectId,
                                    const QVariant &title);
    // editor.py:439-449 removeOverride
    Q_INVOKABLE bool removeOverride(const QString &overrideId);

    // ── Override 查询（编辑器表格用）──────────────────────────
    // editor.py:459-517 getEntryOverride：应用 override 后的条目字典（null 表示不存在）
    Q_INVOKABLE QVariant getEntryOverride(const QString &entryId, const QVariant &week,
                                          int dayOfWeek) const;
    // editor.py:519-542 getOverrideTitle：仅返回 override 显式给出的标题
    Q_INVOKABLE QString getOverrideTitle(const QString &entryId, const QVariant &week,
                                         int dayOfWeek) const;

    // ── Meta 操作（editor.py:544-626）──────────────────────────
    // editor.py:544-562 setStartDate：格式 yyyy-MM-dd
    Q_INVOKABLE bool setStartDate(const QString &dateStr);
    // editor.py:564-588 setTimelineSettings：一次性更新开学日期与最大周期
    Q_INVOKABLE bool setTimelineSettings(const QString &dateStr, int maxWeeks);
    // editor.py:590-597 getStartDate
    Q_INVOKABLE QString getStartDate() const;
    // editor.py:609-619 setMaxWeekCycle
    Q_INVOKABLE bool setMaxWeekCycle(int maxWeeks);
    // editor.py:621-626 getMaxWeekCycle
    Q_INVOKABLE int getMaxWeekCycle() const;

    // ── 保存状态 ───────────────────────────────────────────────
    // editor.py:688-694 markSaved：标记为已保存（打断去抖提交）
    Q_INVOKABLE void markSaved();
    // Debugger/EditSchedule.qml:214 调用 save()（上游 Python 无此方法，
    // 属 QML 消费面的防御性补齐：保存 + markSaved）
    Q_INVOKABLE bool save();

signals:
    void updated();                      // editor.py:36
    void subjectsChanged();              // editor.py:37
    void daysChanged();                  // editor.py:38
    void entriesChanged();               // editor.py:39
    void metaChanged();                  // editor.py:40
    void overridesChanged();             // editor.py:41
    void overridesRevisionChanged();     // editor.py:42
    void dirtyChanged();                 // editor.py:43

private:
    void onUpdated();                                  // editor.py:107-112
    void refresh(const QJsonObject &schedule);         // editor.py:89-105（manager 推送）
    void rebuildCaches();                              // editor.py:114-120
    void emitDaysChanged();                            // editor.py:122-124
    void emitEntriesChanged(const QJsonObject &day);   // editor.py:126-147（空 day = 全量）
    void submitToManager();                            // editor.py:149-150 refresh_manager
    QJsonObject dayById(const QString &dayId) const;
    QJsonObject entryById(const QString &entryId) const;
    QJsonObject subjectById(const QString &subjectId) const;
    QJsonObject overrideById(const QString &overrideId) const;
    bool validateTimeRange(const QString &startTime, const QString &endTime) const; // editor.py:64-87

    ScheduleManager *m_manager = nullptr;
    QJsonObject m_schedule;          // editor.py:49 schedule
    QString m_filename;              // editor.py:48 _filename
    bool m_dirty = false;            // editor.py:50
    int m_entriesRevision = 0;       // editor.py:51
    int m_overridesRevision = 0;     // editor.py:52
    QVariantList m_daysData;         // editor.py:53 _days_data
    QVariantList m_entriesData;      // editor.py:54 _entries_data
    bool m_suppressUpdate = false;   // editor.py:55
    QTimer m_refreshTimer;           // editor.py:56-59（100ms 单发去抖）
};
