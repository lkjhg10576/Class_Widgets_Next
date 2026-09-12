#include "ScheduleEditor.h"

#include "../AppPaths.h"
#include "../Logger.h"
#include "ScheduleManager.h"
#include "ScheduleModel.h"

#include <QJsonArray>

#include <algorithm>
#include <utility>

using namespace ScheduleModel;

namespace {

// QML 的 dayOfWeek 参数归一化：int → [int]；list → 数组；其余 → null。
// 上游 `day_of_week or None`（editor.py:224）对 0/空列表同样落到 null。
QJsonValue normalizeDayOfWeekParam(const QVariant &value)
{
    if (!value.isValid() || value.isNull()) {
        return QJsonValue(QJsonValue::Null);
    }
    if (value.canConvert<int>() && value.metaType().id() == QMetaType::Int) {
        const int v = value.toInt();
        return v > 0 ? QJsonValue(v) : QJsonValue(QJsonValue::Null);
    }
    if (value.metaType().id() == QMetaType::Double) {
        const int v = value.toInt();
        return v > 0 ? QJsonValue(v) : QJsonValue(QJsonValue::Null);
    }
    const QVariantList list = value.toList();
    if (!list.isEmpty()) {
        QJsonArray array;
        for (const QVariant &item : list) {
            if (item.canConvert<int>()) {
                array.append(item.toInt());
            }
        }
        if (!array.isEmpty()) {
            return array;
        }
    }
    return QJsonValue(QJsonValue::Null);
}

bool entryByIdInArray(const QJsonArray &entries, const QString &entryId, QJsonObject *out)
{
    for (const QJsonValue &v : entries) {
        const QJsonObject entry = v.toObject();
        if (ScheduleModel::entryId(entry) == entryId) {
            *out = entry;
            return true;
        }
    }
    return false;
}

} // namespace

ScheduleEditor::ScheduleEditor(ScheduleManager *manager, QObject *parent)
    : QObject(parent)
    , m_manager(manager)
{
    // editor.py:47-60
    m_schedule = m_manager ? m_manager->schedule() : QJsonObject();
    m_filename = m_manager ? m_manager->schedulePathStem() : QString();
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(100);
    connect(&m_refreshTimer, &QTimer::timeout, this, &ScheduleEditor::submitToManager);
    rebuildCaches();
    connect(this, &ScheduleEditor::updated, this, &ScheduleEditor::onUpdated);
    if (m_manager) {
        connect(m_manager, &ScheduleManager::scheduleSwitched, this,
                [this](const QJsonObject &schedule) { refresh(schedule); });
    }
}

bool ScheduleEditor::validateTimeRange(const QString &startTime, const QString &endTime) const
{
    // editor.py:64-87：结束时间必须晚于开始时间
    const QTime start = parseHm(startTime);
    const QTime end = parseHm(endTime);
    if (!start.isValid() || !end.isValid()) {
        cwn::Log::error(QStringLiteral("Invalid time format: %1 - %2").arg(startTime, endTime));
        return false;
    }
    if (end <= start) {
        cwn::Log::warn(QStringLiteral("Invalid time range: end time %1 is not later than "
                                      "start time %2").arg(endTime, startTime));
        return false;
    }
    return true;
}

void ScheduleEditor::onUpdated()
{
    // editor.py:107-112：内容变化 → 置脏并安排提交
    if (m_suppressUpdate) {
        return;
    }
    m_dirty = true;
    emit dirtyChanged();
    m_refreshTimer.start();
}

void ScheduleEditor::refresh(const QJsonObject &schedule)
{
    // editor.py:89-105：接受来自 manager 的更新
    m_refreshTimer.stop();
    m_schedule = schedule;
    m_filename = m_manager ? m_manager->schedulePathStem() : QString();
    rebuildCaches();
    m_dirty = false;
    m_suppressUpdate = true;
    emit updated();
    m_suppressUpdate = false;
    emit subjectsChanged();
    emit daysChanged();
    ++m_entriesRevision;
    emit entriesChanged();
    emit metaChanged();
    emit overridesChanged();
    ++m_overridesRevision;
    emit overridesRevisionChanged();
}

void ScheduleEditor::rebuildCaches()
{
    // editor.py:114-120
    m_daysData.clear();
    m_entriesData.clear();
    if (m_schedule.isEmpty()) {
        return;
    }
    const QJsonArray dayList = ScheduleModel::days(m_schedule);
    m_daysData.reserve(dayList.size());
    for (const QJsonValue &v : dayList) {
        m_daysData.append(v.toObject().toVariantMap());
    }
    m_entriesData = m_daysData; // editor.py:120：初始与 days 共享同一份快照
}

void ScheduleEditor::emitDaysChanged()
{
    // editor.py:122-124
    rebuildCaches();
    emit daysChanged();
}

void ScheduleEditor::emitEntriesChanged(const QJsonObject &day)
{
    // editor.py:126-147：只刷新条目缓存，不重排时间线列表。
    // day 为空对象时做全量同步（如删除科目会影响多个 day）。
    if (m_schedule.isEmpty()) {
        m_entriesData.clear();
    } else if (day.isEmpty()) {
        m_entriesData.clear();
        const QJsonArray dayList = ScheduleModel::days(m_schedule);
        m_entriesData.reserve(dayList.size());
        for (const QJsonValue &v : dayList) {
            m_entriesData.append(v.toObject().toVariantMap());
        }
    } else {
        const QString targetId = day.value(QLatin1String("id")).toString();
        const QJsonArray dayList = ScheduleModel::days(m_schedule);
        int dayIndex = -1;
        for (int i = 0; i < dayList.size(); ++i) {
            if (dayList.at(i).toObject().value(QLatin1String("id")).toString() == targetId) {
                dayIndex = i;
                break;
            }
        }
        if (dayIndex < 0) {
            m_entriesData.clear();
            for (const QJsonValue &v : dayList) {
                m_entriesData.append(v.toObject().toVariantMap());
            }
        } else {
            m_entriesData[dayIndex] = day.toVariantMap();
        }
    }
    ++m_entriesRevision;
    emit entriesChanged();
}

void ScheduleEditor::submitToManager()
{
    // editor.py:149-150 refresh_manager：提交给 manager
    if (m_manager) {
        m_manager->modify(m_schedule);
    }
}

// ── Subject 操作 ─────────────────────────────────────────────

QString ScheduleEditor::addSubject(const QString &name, const QString &teacher,
                                   const QString &icon, const QString &color,
                                   const QString &location, bool isLocalClassroom)
{
    // editor.py:153-169 addSubject
    QJsonObject subject;
    subject.insert(QLatin1String("id"), generateId(QStringLiteral("subj")));
    subject.insert(QLatin1String("name"), name);
    subject.insert(QLatin1String("simplifiedName"), QJsonValue(QJsonValue::Null));
    subject.insert(QLatin1String("teacher"), teacher.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                               : QJsonValue(teacher));
    subject.insert(QLatin1String("icon"), icon.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                         : QJsonValue(icon));
    subject.insert(QLatin1String("color"), color.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                           : QJsonValue(color));
    subject.insert(QLatin1String("location"), location.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                                 : QJsonValue(location));
    subject.insert(QLatin1String("isLocalClassroom"), isLocalClassroom);

    QJsonArray subjectList = ScheduleModel::subjects(m_schedule);
    subjectList.append(subject);
    m_schedule.insert(QLatin1String("subjects"), subjectList);
    emit updated();
    emit subjectsChanged();
    return ScheduleModel::entryId(subject); // id 字段同源，复用读取器
}

void ScheduleEditor::updateSubject(const QString &subjectId, const QString &name,
                                   const QString &simplifiedName, const QString &teacher,
                                   const QString &icon, const QString &color,
                                   const QString &location, bool isLocalClassroom)
{
    // editor.py:171-194 updateSubject
    QJsonObject subject = subjectById(subjectId);
    if (subject.isEmpty()) {
        return;
    }
    const QString oldName = subject.value(QLatin1String("name")).toString();
    const QString oldSimplified = subject.value(QLatin1String("simplifiedName")).toString();
    const QString oldIcon = subject.value(QLatin1String("icon")).toString();
    const QString oldColor = subject.value(QLatin1String("color")).toString();
    const QString oldTeacher = subject.value(QLatin1String("teacher")).toString();
    const QString oldLocation = subject.value(QLatin1String("location")).toString();
    const bool oldLocal = subject.value(QLatin1String("isLocalClassroom")).toBool(true);

    // `name or subject.name`：空串保留旧值；icon/color/teacher/location 直接落 None
    const QString newName = name.isEmpty() ? oldName : name;
    const QString newSimplified = simplifiedName.isEmpty() ? oldSimplified : simplifiedName;

    // editor.py:188-189：全部字段一致时不做任何变更
    const bool changed = newName != oldName || newSimplified != oldSimplified
        || icon != oldIcon || color != oldColor || teacher != oldTeacher
        || location != oldLocation || isLocalClassroom != oldLocal;
    if (!changed) {
        return;
    }

    subject.insert(QLatin1String("name"), newName);
    subject.insert(QLatin1String("simplifiedName"),
                   newSimplified.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(newSimplified));
    subject.insert(QLatin1String("icon"), icon.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                         : QJsonValue(icon));
    subject.insert(QLatin1String("color"), color.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                           : QJsonValue(color));
    subject.insert(QLatin1String("teacher"), teacher.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                               : QJsonValue(teacher));
    subject.insert(QLatin1String("location"), location.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                                 : QJsonValue(location));
    subject.insert(QLatin1String("isLocalClassroom"), isLocalClassroom);

    QJsonArray subjectList = ScheduleModel::subjects(m_schedule);
    for (int i = 0; i < subjectList.size(); ++i) {
        if (subjectList.at(i).toObject().value(QLatin1String("id")).toString() == subjectId) {
            subjectList[i] = subject;
            break;
        }
    }
    m_schedule.insert(QLatin1String("subjects"), subjectList);
    emit updated();
    emit subjectsChanged();
}

void ScheduleEditor::removeSubject(const QString &subjectId)
{
    // editor.py:196-210 removeSubject：同时删除相关的课程条目
    if (subjectById(subjectId).isEmpty()) {
        return;
    }
    const QJsonArray dayList = ScheduleModel::days(m_schedule);
    QJsonArray newDays;
    for (const QJsonValue &dayValue : dayList) {
        QJsonObject day = dayValue.toObject();
        QJsonArray kept;
        const QJsonArray entries = day.value(QLatin1String("entries")).toArray();
        for (const QJsonValue &entryValue : entries) {
            if (ScheduleModel::entrySubjectId(entryValue.toObject()) != subjectId) {
                kept.append(entryValue);
            }
        }
        day.insert(QLatin1String("entries"), kept);
        newDays.append(day);
    }
    m_schedule.insert(QLatin1String("days"), newDays);

    QJsonArray subjectList = ScheduleModel::subjects(m_schedule);
    QJsonArray keptSubjects;
    for (const QJsonValue &v : subjectList) {
        if (v.toObject().value(QLatin1String("id")).toString() != subjectId) {
            keptSubjects.append(v);
        }
    }
    m_schedule.insert(QLatin1String("subjects"), keptSubjects);

    emitEntriesChanged(QJsonObject()); // 全量同步
    emit updated();
    emit subjectsChanged();
}

QVariant ScheduleEditor::getSubject(const QString &subjectId) const
{
    // editor.py:212-215
    const QJsonObject subject = subjectById(subjectId);
    return subject.isEmpty() ? QVariant() : QVariant(subject.toVariantMap());
}

QString ScheduleEditor::subjectNameById(const QString &subjectId) const
{
    // editor.py:451-457
    const QJsonObject subject = subjectById(subjectId);
    if (!subject.isEmpty()) {
        return subject.value(QLatin1String("name")).toString();
    }
    return QString();
}

void ScheduleEditor::restoreDefaultSubjects()
{
    // editor.py:599-607 restoreDefaultSubjects
    m_schedule.insert(QLatin1String("subjects"), defaultSubjects());
    emit updated();
    emit subjectsChanged();
}

// ── Day 操作 ─────────────────────────────────────────────────

QString ScheduleEditor::addDay(const QVariant &dayOfWeek, const QVariant &weeks,
                               const QVariant &date)
{
    // editor.py:218-233 addDay
    QJsonObject day;
    day.insert(QLatin1String("id"), generateId(QStringLiteral("day")));
    day.insert(QLatin1String("entries"), QJsonArray());
    day.insert(QLatin1String("dayOfWeek"), normalizeDayOfWeekParam(dayOfWeek));
    day.insert(QLatin1String("weeks"), toJsonValue(weeks));
    const QString isoDate = dateParamToIso(date);
    day.insert(QLatin1String("date"), isoDate.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                        : QJsonValue(isoDate));

    QJsonArray dayList = ScheduleModel::days(m_schedule);
    dayList.append(day);
    m_schedule.insert(QLatin1String("days"), dayList);
    emitDaysChanged();
    ++m_entriesRevision;
    emit entriesChanged();
    emit updated();
    return day.value(QLatin1String("id")).toString();
}

void ScheduleEditor::updateDay(const QString &dayId, const QVariant &dayOfWeek,
                               const QVariant &weeks, const QVariant &date)
{
    // editor.py:235-256 updateDay：提交完整模式状态，清除前一模式残留
    QJsonObject day = dayById(dayId);
    if (day.isEmpty()) {
        return;
    }
    day.insert(QLatin1String("dayOfWeek"), normalizeDayOfWeekParam(dayOfWeek));
    if (!weeks.isNull() && weeks.isValid()) {
        // editor.py:246-251："all" 字符串原样存储，其余归一化存储
        day.insert(QLatin1String("weeks"), toJsonValue(weeks));
    }
    const QString isoDate = dateParamToIso(date);
    day.insert(QLatin1String("date"), isoDate.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                        : QJsonValue(isoDate));

    QJsonArray dayList = ScheduleModel::days(m_schedule);
    for (int i = 0; i < dayList.size(); ++i) {
        if (dayList.at(i).toObject().value(QLatin1String("id")).toString() == dayId) {
            dayList[i] = day;
            break;
        }
    }
    m_schedule.insert(QLatin1String("days"), dayList);
    emitDaysChanged();
    ++m_entriesRevision;
    emit entriesChanged();
    emit updated();
}

void ScheduleEditor::removeDay(const QString &dayId)
{
    // editor.py:258-270 removeDay
    if (dayById(dayId).isEmpty()) {
        cwn::Log::warn(QStringLiteral("Day: %1 not found").arg(dayId));
        return;
    }
    QJsonArray dayList = ScheduleModel::days(m_schedule);
    QJsonArray kept;
    for (const QJsonValue &v : dayList) {
        if (v.toObject().value(QLatin1String("id")).toString() != dayId) {
            kept.append(v);
        }
    }
    m_schedule.insert(QLatin1String("days"), kept);
    emitDaysChanged();
    ++m_entriesRevision;
    emit entriesChanged();
    emit updated();
}

QString ScheduleEditor::duplicateDay(const QString &dayId)
{
    // editor.py:272-294 duplicateDay
    const QJsonObject original = dayById(dayId);
    if (original.isEmpty()) {
        cwn::Log::warn(QStringLiteral("Day to duplicate not found: %1").arg(dayId));
        return QString();
    }
    QJsonObject newDay = original;
    newDay.insert(QLatin1String("id"), generateId(QStringLiteral("day")));
    QJsonArray entries = original.value(QLatin1String("entries")).toArray();
    for (int i = 0; i < entries.size(); ++i) {
        QJsonObject entry = entries.at(i).toObject();
        entry.insert(QLatin1String("id"), generateId(QStringLiteral("entry")));
        entries[i] = entry;
    }
    newDay.insert(QLatin1String("entries"), entries);

    QJsonArray dayList = ScheduleModel::days(m_schedule);
    dayList.append(newDay);
    m_schedule.insert(QLatin1String("days"), dayList);
    emitDaysChanged();
    ++m_entriesRevision;
    emit entriesChanged();
    emit updated();
    return newDay.value(QLatin1String("id")).toString();
}

QVariant ScheduleEditor::getDay(const QString &dayId) const
{
    // editor.py:296-299
    const QJsonObject day = dayById(dayId);
    return day.isEmpty() ? QVariant() : QVariant(day.toVariantMap());
}

// ── Entry 操作 ───────────────────────────────────────────────

QString ScheduleEditor::addEntry(const QString &dayId, const QString &entryType,
                                 const QString &startTime, const QString &endTime,
                                 const QString &subjectId, const QString &title)
{
    // editor.py:302-328 addEntry
    QJsonObject day = dayById(dayId);
    if (day.isEmpty()) {
        return QString();
    }
    if (!validateTimeRange(startTime, endTime)) {
        cwn::Log::error(QStringLiteral("Cannot add entry: invalid time range %1 - %2")
                            .arg(startTime, endTime));
        return QString();
    }

    QJsonObject entry;
    entry.insert(QLatin1String("id"), generateId(QStringLiteral("entry")));
    entry.insert(QLatin1String("type"), entryType);
    entry.insert(QLatin1String("startTime"), startTime);
    entry.insert(QLatin1String("endTime"), endTime);
    entry.insert(QLatin1String("subjectId"), subjectId.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                                 : QJsonValue(subjectId));
    entry.insert(QLatin1String("title"), title.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                         : QJsonValue(title));

    QJsonArray entries = day.value(QLatin1String("entries")).toArray();
    entries.append(entry);
    // editor.py:325：按 startTime 字符串排序（"HH:MM" 字典序即时间序）
    QList<QJsonObject> sorted;
    for (const QJsonValue &v : entries) {
        sorted.append(v.toObject());
    }
    std::stable_sort(sorted.begin(), sorted.end(), [](const QJsonObject &a, const QJsonObject &b) {
        return ScheduleModel::entryStartTime(a) < ScheduleModel::entryStartTime(b);
    });
    entries = QJsonArray();
    for (const QJsonObject &e : sorted) {
        entries.append(e);
    }
    day.insert(QLatin1String("entries"), entries);

    QJsonArray dayList = ScheduleModel::days(m_schedule);
    for (int i = 0; i < dayList.size(); ++i) {
        if (dayList.at(i).toObject().value(QLatin1String("id")).toString() == dayId) {
            dayList[i] = day;
            break;
        }
    }
    m_schedule.insert(QLatin1String("days"), dayList);
    emitEntriesChanged(day);
    emit updated();
    return ScheduleModel::entryId(entry);
}

void ScheduleEditor::updateEntry(const QString &entryId, const QString &entryType,
                                 const QString &startTime, const QString &endTime,
                                 const QString &subjectId, const QString &title)
{
    // editor.py:330-366 updateEntry
    QJsonObject entry = entryById(entryId);
    if (entry.isEmpty()) {
        cwn::Log::warn(QStringLiteral("Entry: %1 not found").arg(entryId));
        return;
    }
    const QString currentStart = startTime.isEmpty()
        ? ScheduleModel::entryStartTime(entry) : startTime;
    const QString currentEnd = endTime.isEmpty()
        ? ScheduleModel::entryEndTime(entry) : endTime;
    if (!startTime.isEmpty() || !endTime.isEmpty()) {
        if (!validateTimeRange(currentStart, currentEnd)) {
            cwn::Log::error(QStringLiteral("Cannot update entry: invalid time range %1 - %2")
                                .arg(currentStart, currentEnd));
            return;
        }
    }
    if (!entryType.isEmpty()) {
        entry.insert(QLatin1String("type"), entryType);
    }
    if (!startTime.isEmpty()) {
        entry.insert(QLatin1String("startTime"), startTime);
    }
    if (!endTime.isEmpty()) {
        entry.insert(QLatin1String("endTime"), endTime);
    }
    entry.insert(QLatin1String("subjectId"), subjectId.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                                 : QJsonValue(subjectId));
    entry.insert(QLatin1String("title"), title.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                         : QJsonValue(title));

    // 找到所属 day 并重排（editor.py:359-364）
    QJsonArray dayList = ScheduleModel::days(m_schedule);
    QJsonObject changedDay;
    for (int i = 0; i < dayList.size(); ++i) {
        QJsonObject day = dayList.at(i).toObject();
        QJsonArray entries = day.value(QLatin1String("entries")).toArray();
        bool contains = false;
        for (const QJsonValue &v : entries) {
            if (ScheduleModel::entryId(v.toObject()) == entryId) {
                contains = true;
                break;
            }
        }
        if (!contains) {
            continue;
        }
        QList<QJsonObject> sorted;
        for (const QJsonValue &v : entries) {
            QJsonObject e = v.toObject();
            if (ScheduleModel::entryId(e) == entryId) {
                e = entry; // 用新内容替换
            }
            sorted.append(e);
        }
        std::stable_sort(sorted.begin(), sorted.end(), [](const QJsonObject &a, const QJsonObject &b) {
            return ScheduleModel::entryStartTime(a) < ScheduleModel::entryStartTime(b);
        });
        QJsonArray newEntries;
        for (const QJsonObject &e : sorted) {
            newEntries.append(e);
        }
        day.insert(QLatin1String("entries"), newEntries);
        dayList[i] = day;
        changedDay = day;
        break;
    }
    m_schedule.insert(QLatin1String("days"), dayList);
    emitEntriesChanged(changedDay);
    emit updated();
}

void ScheduleEditor::removeEntry(const QString &entryId)
{
    // editor.py:368-377 removeEntry
    QJsonArray dayList = ScheduleModel::days(m_schedule);
    for (int i = 0; i < dayList.size(); ++i) {
        QJsonObject day = dayList.at(i).toObject();
        QJsonArray entries = day.value(QLatin1String("entries")).toArray();
        QJsonArray kept;
        bool removed = false;
        for (const QJsonValue &v : entries) {
            if (ScheduleModel::entryId(v.toObject()) == entryId) {
                removed = true;
                continue;
            }
            kept.append(v);
        }
        if (removed) {
            day.insert(QLatin1String("entries"), kept);
            dayList[i] = day;
            m_schedule.insert(QLatin1String("days"), dayList);
            emitEntriesChanged(day);
            emit updated();
            return;
        }
    }
}

QVariant ScheduleEditor::getEntry(const QString &entryId) const
{
    // editor.py:379-386
    const QJsonObject entry = entryById(entryId);
    return entry.isEmpty() ? QVariant() : QVariant(entry.toVariantMap());
}

// ── Override 操作 ────────────────────────────────────────────

QString ScheduleEditor::findOverride(const QString &entryId, const QVariant &dayOfWeek,
                                     const QVariant &weeks) const
{
    // editor.py:389-404 findOverride：精确匹配 (entryId, dayOfWeek, weeks)
    const QJsonValue wantDow = normalizeDayOfWeekParam(dayOfWeek);
    const QJsonValue wantWeeks = toJsonValue(weeks);
    const QJsonArray overrideList = ScheduleModel::overrides(m_schedule);
    for (const QJsonValue &v : overrideList) {
        const QJsonObject override_ = v.toObject();
        if (overrideEntryId(override_) != entryId) {
            continue;
        }
        if (override_.value(QLatin1String("dayOfWeek")) != wantDow) {
            continue;
        }
        if (override_.value(QLatin1String("weeks")) != wantWeeks) {
            continue;
        }
        return override_.value(QLatin1String("id")).toString();
    }
    return QString();
}

bool ScheduleEditor::addOverride(const QString &entryId, const QVariant &dayOfWeek,
                                 const QVariant &weeks, const QString &subjectId,
                                 const QString &title)
{
    // editor.py:406-422 addOverride
    QJsonObject override_;
    override_.insert(QLatin1String("id"), generateId(QStringLiteral("override")));
    override_.insert(QLatin1String("entryId"), entryId);
    override_.insert(QLatin1String("dayOfWeek"), normalizeDayOfWeekParam(dayOfWeek));
    override_.insert(QLatin1String("weeks"), toJsonValue(weeks));
    override_.insert(QLatin1String("subjectId"), subjectId.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                                     : QJsonValue(subjectId));
    override_.insert(QLatin1String("title"), title.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                             : QJsonValue(title));
    override_.insert(QLatin1String("startTime"), QJsonValue(QJsonValue::Null));
    override_.insert(QLatin1String("endTime"), QJsonValue(QJsonValue::Null));

    QJsonArray overrideList = ScheduleModel::overrides(m_schedule);
    overrideList.append(override_);
    m_schedule.insert(QLatin1String("overrides"), overrideList);
    emit overridesChanged();
    ++m_overridesRevision;
    emit overridesRevisionChanged();
    emit updated();
    return true;
}

bool ScheduleEditor::updateOverride(const QString &overrideId, const QVariant &subjectId,
                                    const QVariant &title)
{
    // editor.py:424-437 updateOverride：null 参数不修改对应字段
    QJsonArray overrideList = ScheduleModel::overrides(m_schedule);
    for (int i = 0; i < overrideList.size(); ++i) {
        QJsonObject override_ = overrideList.at(i).toObject();
        if (override_.value(QLatin1String("id")).toString() != overrideId) {
            continue;
        }
        if (!subjectId.isNull() && subjectId.isValid()) {
            const QString sid = subjectId.toString();
            override_.insert(QLatin1String("subjectId"),
                             sid.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(sid));
        }
        if (!title.isNull() && title.isValid()) {
            const QString t = title.toString();
            override_.insert(QLatin1String("title"),
                             t.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(t));
        }
        overrideList[i] = override_;
        m_schedule.insert(QLatin1String("overrides"), overrideList);
        emit overridesChanged();
        ++m_overridesRevision;
        emit overridesRevisionChanged();
        emit updated();
        return true;
    }
    return false;
}

bool ScheduleEditor::removeOverride(const QString &overrideId)
{
    // editor.py:439-449 removeOverride
    QJsonArray overrideList = ScheduleModel::overrides(m_schedule);
    QJsonArray kept;
    bool removed = false;
    for (const QJsonValue &v : overrideList) {
        if (v.toObject().value(QLatin1String("id")).toString() == overrideId) {
            removed = true;
            continue;
        }
        kept.append(v);
    }
    if (!removed) {
        return false;
    }
    m_schedule.insert(QLatin1String("overrides"), kept);
    emit overridesChanged();
    ++m_overridesRevision;
    emit overridesRevisionChanged();
    emit updated();
    return true;
}

QVariant ScheduleEditor::getEntryOverride(const QString &entryId, const QVariant &week,
                                          int dayOfWeek) const
{
    // editor.py:459-517 getEntryOverride
    const QJsonObject entry = entryById(entryId);
    if (entry.isEmpty()) {
        return QVariant();
    }
    QJsonObject data = entry;

    // 当 week 是列表时，拆成单个元素逐个匹配（editor.py:469-470）
    const QJsonValue weekValue = toJsonValue(week);
    QList<int> weekList;
    if (weekValue.isArray()) {
        for (const QJsonValue &v : weekValue.toArray()) {
            if (v.isDouble()) {
                weekList.append(v.toInt());
            }
        }
    } else if (weekValue.isDouble()) {
        weekList.append(weekValue.toInt());
    }

    const int maxCycle = maxWeekCycle(m_schedule);
    // (priority, override) 收集（editor.py:472-497）
    QList<QPair<int, QJsonObject>> applicable;
    const QJsonArray overrideList = ScheduleModel::overrides(m_schedule);
    for (const QJsonValue &v : overrideList) {
        const QJsonObject override_ = v.toObject();
        if (overrideEntryId(override_) != entryId) {
            continue;
        }
        const QJsonValue dow = override_.value(QLatin1String("dayOfWeek"));
        if (!dow.isNull() && !dow.isUndefined()) {
            if (!dayOfWeekList(dow).contains(QString::number(dayOfWeek))) {
                continue;
            }
        }
        const QJsonValue weeks = override_.value(QLatin1String("weeks"));
        if (weeks.isArray()) {
            bool hit = false;
            for (const QJsonValue &w : weeks.toArray()) {
                if (w.isDouble() && weekList.contains(w.toInt())) {
                    hit = true;
                    break;
                }
            }
            if (!hit) {
                continue;
            }
            applicable.append({3, override_});
        } else if (weeks.isDouble()) {
            const int start = weeks.toInt();
            bool hit = false;
            for (int w : weekList) {
                if (w >= start && ((w - start) % maxCycle) == 0) {
                    hit = true;
                    break;
                }
            }
            if (!hit) {
                continue;
            }
            applicable.append({2, override_});
        } else if (weeks.toString() == QLatin1String("all") || weeks.isNull()
                   || weeks.isUndefined()) {
            applicable.append({1, override_});
        } else {
            continue;
        }
    }

    // 按优先级从低到高逐字段应用（editor.py:499-511）
    std::stable_sort(applicable.begin(), applicable.end(),
              [](const QPair<int, QJsonObject> &a, const QPair<int, QJsonObject> &b) {
                  return a.first < b.first;
              });
    bool subjectOverridden = false;
    bool titleOverridden = false;
    for (const auto &item : applicable) {
        const QJsonObject override_ = item.second;
        const QString subject = override_.value(QLatin1String("subjectId")).toString();
        const QString title = override_.value(QLatin1String("title")).toString();
        if (!subject.isEmpty()) {
            data.insert(QLatin1String("subjectId"), subject);
            subjectOverridden = true;
        }
        if (!title.isEmpty()) {
            data.insert(QLatin1String("title"), title);
            titleOverridden = true;
        }
    }
    // 科目 override 取代时间线隐式标签，但保留显式标题（editor.py:513-515）
    if (subjectOverridden && !titleOverridden) {
        data.insert(QLatin1String("title"), QJsonValue(QJsonValue::Null));
    }
    return data.toVariantMap();
}

QString ScheduleEditor::getOverrideTitle(const QString &entryId, const QVariant &week,
                                         int dayOfWeek) const
{
    // editor.py:519-542 getOverrideTitle
    const QJsonValue weekValue = toJsonValue(week);
    QList<int> weekList;
    if (weekValue.isArray()) {
        for (const QJsonValue &v : weekValue.toArray()) {
            if (v.isDouble()) {
                weekList.append(v.toInt());
            }
        }
    } else if (weekValue.isDouble()) {
        weekList.append(weekValue.toInt());
    }

    const int maxCycle = maxWeekCycle(m_schedule);
    QList<QPair<int, QString>> titles;
    const QJsonArray overrideList = ScheduleModel::overrides(m_schedule);
    for (const QJsonValue &v : overrideList) {
        const QJsonObject override_ = v.toObject();
        if (overrideEntryId(override_) != entryId) {
            continue;
        }
        const QJsonValue dow = override_.value(QLatin1String("dayOfWeek"));
        if (!dow.isNull() && !dow.isUndefined()
            && !dayOfWeekList(dow).contains(QString::number(dayOfWeek))) {
            continue;
        }
        // editor.py:528-539：按 weeks 形态计算优先级（3=列表 > 2=int 周期 > 1=all/空）
        const QJsonValue weeks = override_.value(QLatin1String("weeks"));
        int priority = -1;
        if (weeks.isArray()) {
            for (const QJsonValue &w : weeks.toArray()) {
                if (w.isDouble() && weekList.contains(w.toInt())) {
                    priority = 3;
                    break;
                }
            }
        } else if (weeks.isDouble()) {
            const int start = weeks.toInt();
            for (int w : weekList) {
                if (w >= start && ((w - start) % maxCycle) == 0) {
                    priority = 2;
                    break;
                }
            }
        } else if (weeks.toString() == QLatin1String("all") || weeks.isNull()
                   || weeks.isUndefined()) {
            priority = 1;
        }
        if (priority < 0) {
            continue;
        }
        const QString title = override_.value(QLatin1String("title")).toString();
        if (!title.isEmpty()) {
            titles.append({priority, title});
        }
    }
    if (titles.isEmpty()) {
        return QString();
    }
    std::stable_sort(titles.begin(), titles.end(),
              [](const QPair<int, QString> &a, const QPair<int, QString> &b) {
                  return a.first < b.first;
              });
    return titles.last().second; // 优先级最高的显式标题
}

// ── Meta 操作 ────────────────────────────────────────────────

bool ScheduleEditor::setStartDate(const QString &dateStr)
{
    // editor.py:544-562 setStartDate
    if (!QDate::fromString(dateStr, QStringLiteral("yyyy-MM-dd")).isValid()) {
        cwn::Log::warn(QStringLiteral("Invalid date format: %1").arg(dateStr));
        return false;
    }
    if (m_schedule.isEmpty()) {
        cwn::Log::warn(QStringLiteral("No schedule or meta data available."));
        return false;
    }
    QJsonObject metaObj = ScheduleModel::meta(m_schedule);
    metaObj.insert(QLatin1String("startDate"), dateStr);
    m_schedule.insert(QLatin1String("meta"), metaObj);
    emit metaChanged();
    emit updated();
    return true;
}

bool ScheduleEditor::setTimelineSettings(const QString &dateStr, int maxWeeks)
{
    // editor.py:564-588 setTimelineSettings：一次性更新，避免多次全局刷新
    if (!QDate::fromString(dateStr, QStringLiteral("yyyy-MM-dd")).isValid()) {
        cwn::Log::warn(QStringLiteral("Invalid date format: %1").arg(dateStr));
        return false;
    }
    if (m_schedule.isEmpty() || maxWeeks < 1) {
        cwn::Log::warn(QStringLiteral("Invalid schedule meta data or max week cycle."));
        return false;
    }
    QJsonObject metaObj = ScheduleModel::meta(m_schedule);
    const bool changed = metaObj.value(QLatin1String("startDate")).toString() != dateStr
        || metaMaxWeekCycle(metaObj) != maxWeeks;
    if (!changed) {
        return true;
    }
    metaObj.insert(QLatin1String("startDate"), dateStr);
    metaObj.insert(QLatin1String("maxWeekCycle"), maxWeeks);
    m_schedule.insert(QLatin1String("meta"), metaObj);
    emit metaChanged();
    emit updated();
    return true;
}

QString ScheduleEditor::getStartDate() const
{
    // editor.py:590-597 getStartDate
    if (m_schedule.isEmpty()) {
        return QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
    }
    const QString value = startDate(m_schedule);
    return value.isEmpty() ? QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"))
                           : value;
}

bool ScheduleEditor::setMaxWeekCycle(int maxWeeks)
{
    // editor.py:609-619 setMaxWeekCycle
    if (m_schedule.isEmpty()) {
        cwn::Log::warn(QStringLiteral("No schedule or meta data available."));
        return false;
    }
    QJsonObject metaObj = ScheduleModel::meta(m_schedule);
    metaObj.insert(QLatin1String("maxWeekCycle"), maxWeeks);
    m_schedule.insert(QLatin1String("meta"), metaObj);
    emit metaChanged();
    emit updated();
    return true;
}

int ScheduleEditor::getMaxWeekCycle() const
{
    // editor.py:621-626 getMaxWeekCycle
    if (m_schedule.isEmpty()) {
        return 1;
    }
    return metaMaxWeekCycle(ScheduleModel::meta(m_schedule));
}

// ── 保存状态 ─────────────────────────────────────────────────

void ScheduleEditor::markSaved()
{
    // editor.py:688-694 markSaved
    m_refreshTimer.stop();
    if (m_dirty) {
        m_dirty = false;
        emit dirtyChanged();
    }
}

bool ScheduleEditor::save()
{
    // Debugger/EditSchedule.qml:214 的 save() 调用：保存到磁盘并复位脏状态
    const bool ok = m_manager ? m_manager->save() : false;
    markSaved();
    return ok;
}

// ── 属性读取器 ───────────────────────────────────────────────

QVariant ScheduleEditor::meta() const
{
    // editor.py:629-634
    if (m_schedule.isEmpty()) {
        return QVariantMap();
    }
    return ScheduleModel::meta(m_schedule).toVariantMap();
}

QVariantList ScheduleEditor::subjects() const
{
    // editor.py:636-641
    if (m_schedule.isEmpty()) {
        return {};
    }
    QVariantList result;
    const QJsonArray list = ScheduleModel::subjects(m_schedule);
    result.reserve(list.size());
    for (const QJsonValue &v : list) {
        result.append(v.toObject().toVariantMap());
    }
    return result;
}

QVariantList ScheduleEditor::overrides() const
{
    // editor.py:658-664
    if (m_schedule.isEmpty()) {
        return {};
    }
    QVariantList result;
    const QJsonArray list = ScheduleModel::overrides(m_schedule);
    result.reserve(list.size());
    for (const QJsonValue &v : list) {
        result.append(v.toObject().toVariantMap());
    }
    return result;
}

QString ScheduleEditor::path() const
{
    // editor.py:678-681：schedule_path.as_uri()
    if (!m_manager) {
        return QString();
    }
    return AppPaths::instance().uriOf(m_manager->schedulePath());
}

// ── 内部查找 ─────────────────────────────────────────────────

QJsonObject ScheduleEditor::dayById(const QString &dayId) const
{
    const QJsonArray dayList = ScheduleModel::days(m_schedule);
    for (const QJsonValue &v : dayList) {
        const QJsonObject day = v.toObject();
        if (day.value(QLatin1String("id")).toString() == dayId) {
            return day;
        }
    }
    return {};
}

QJsonObject ScheduleEditor::entryById(const QString &entryId) const
{
    const QJsonArray dayList = ScheduleModel::days(m_schedule);
    for (const QJsonValue &v : dayList) {
        QJsonObject entry;
        if (entryByIdInArray(v.toObject().value(QLatin1String("entries")).toArray(),
                             entryId, &entry)) {
            return entry;
        }
    }
    return {};
}

QJsonObject ScheduleEditor::subjectById(const QString &subjectId) const
{
    const QJsonArray subjectList = ScheduleModel::subjects(m_schedule);
    for (const QJsonValue &v : subjectList) {
        const QJsonObject subject = v.toObject();
        if (subject.value(QLatin1String("id")).toString() == subjectId) {
            return subject;
        }
    }
    return {};
}

QJsonObject ScheduleEditor::overrideById(const QString &overrideId) const
{
    const QJsonArray overrideList = ScheduleModel::overrides(m_schedule);
    for (const QJsonValue &v : overrideList) {
        const QJsonObject override_ = v.toObject();
        if (override_.value(QLatin1String("id")).toString() == overrideId) {
            return override_;
        }
    }
    return {};
}
