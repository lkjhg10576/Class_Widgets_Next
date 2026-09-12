#include "ClassSwapManager.h"

#include "../ConfigStore.h"
#include "../Logger.h"
#include "ScheduleManager.h"
#include "ScheduleModel.h"

#include <QDate>
#include <QDateTime>
#include <QJsonArray>

using namespace ScheduleModel;

namespace {

QJsonValue configValue(const ConfigStore *configs, const QString &key)
{
    if (!configs) {
        return QJsonValue(QJsonValue::Undefined);
    }
    const std::optional<QJsonValue> value = configs->value(key);
    return value ? *value : QJsonValue(QJsonValue::Undefined);
}

QJsonObject classSwapData(const ConfigStore *configs)
{
    return configValue(configs, QStringLiteral("schedule.class_swap")).toObject();
}

QString todayString()
{
    return QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
}

// days/int 值的字符串比较辅助
bool intInDayOfWeek(const QJsonValue &dayOfWeek, int day)
{
    if (dayOfWeek.isDouble()) {
        return dayOfWeek.toInt() == day;
    }
    const QStringList list = dayOfWeekList(dayOfWeek);
    return list.contains(QString::number(day));
}

} // namespace

ClassSwapManager::ClassSwapManager(ConfigStore *configs, ScheduleManager *manager,
                                   QObject *parent)
    : QObject(parent)
    , m_configs(configs)
    , m_manager(manager)
{
}

// ── 数据查询 ─────────────────────────────────────────────────

QVariantList ClassSwapManager::getDayEntries(int dayOfWeek, int weekOfCycle)
{
    // swapper.py:44-56 getDayEntries
    return dayEntriesInternal(dayOfWeek, weekOfCycle, /*includeNonClass=*/false);
}

QVariantList ClassSwapManager::getAllSubjects()
{
    // swapper.py:58-61 getAllSubjects
    QVariantList result;
    const QJsonArray subjectList = ScheduleModel::subjects(m_manager->schedule());
    result.reserve(subjectList.size());
    for (const QJsonValue &v : subjectList) {
        result.append(v.toObject().toVariantMap());
    }
    return result;
}

int ClassSwapManager::getCurrentDayOfWeek()
{
    // swapper.py:63-66
    return QDate::currentDate().dayOfWeek(); // 1~7（ISO）
}

int ClassSwapManager::getCurrentWeekOfCycle()
{
    // swapper.py:68-75
    const QJsonObject schedule = m_manager->schedule();
    const QString start = startDate(schedule);
    if (schedule.isEmpty() || start.isEmpty()) {
        return 1;
    }
    const int week = weekNumber(start, QDate::currentDate());
    return cycleWeek(week, maxWeekCycle(schedule));
}

int ClassSwapManager::getPreferredDayOfWeek()
{
    // swapper.py:77-85
    const QJsonObject swapData = classSwapData(m_configs);
    if (!swapData.isEmpty()) {
        const QJsonValue value = swapData.value(QLatin1String("day_of_week"));
        if (value.isDouble() && value.toInt() >= 1 && value.toInt() <= 7) {
            return value.toInt();
        }
    }
    return getCurrentDayOfWeek();
}

int ClassSwapManager::getPreferredWeekOfCycle()
{
    // swapper.py:87-96
    const int maxCycle = getMaxWeekCycle();
    const QJsonObject swapData = classSwapData(m_configs);
    if (!swapData.isEmpty()) {
        const QJsonValue value = swapData.value(QLatin1String("week_of_cycle"));
        if (value.isDouble() && value.toInt() >= 1 && value.toInt() <= maxCycle) {
            return value.toInt();
        }
    }
    return getCurrentWeekOfCycle();
}

void ClassSwapManager::setSwapPickerContext(int dayOfWeek, int weekOfCycle)
{
    // swapper.py:98-115
    if (dayOfWeek < 1 || dayOfWeek > 7) {
        return;
    }
    const int maxCycle = getMaxWeekCycle();
    if (weekOfCycle < 1 || weekOfCycle > maxCycle) {
        weekOfCycle = 1;
    }
    QJsonObject swapData = classSwapData(m_configs);
    if (swapData.isEmpty()) {
        swapData = QJsonObject();
    }
    swapData.insert(QLatin1String("day_of_week"), dayOfWeek);
    swapData.insert(QLatin1String("week_of_cycle"), weekOfCycle);
    swapData.insert(QLatin1String("date"), todayString());
    if (m_configs) {
        m_configs->set(QStringLiteral("schedule.class_swap"), swapData);
    }
}

bool ClassSwapManager::applyPickerToToday(int dayOfWeek, int weekOfCycle)
{
    // swapper.py:117-147 applyPickerToToday
    cwn::Log::debug(QStringLiteral("Applying picker context to today: day_of_week=%1, "
                                   "week_of_cycle=%2").arg(dayOfWeek).arg(weekOfCycle));
    const QJsonObject schedule = m_manager->schedule();
    if (schedule.isEmpty()) {
        return false;
    }
    const int maxCycle = maxWeekCycle(schedule);
    if (dayOfWeek < 1 || dayOfWeek > 7) {
        return false;
    }
    if (weekOfCycle < 1 || weekOfCycle > maxCycle) {
        weekOfCycle = 1;
    }

    setSwapPickerContext(dayOfWeek, weekOfCycle);

    // ComboBox 切换时，先清理当前选择时间线已有的临时 swap 覆盖，再整天投射
    clearTodaySwapOverrides(dayOfWeek, weekOfCycle);
    applyDayScheduleToToday(dayOfWeek, weekOfCycle, dayOfWeek, weekOfCycle);

    emit updated();
    return true;
}

int ClassSwapManager::getMaxWeekCycle()
{
    // swapper.py:149-155
    const QJsonObject schedule = m_manager->schedule();
    if (schedule.isEmpty()) {
        return 1;
    }
    return maxWeekCycle(schedule);
}

QString ClassSwapManager::getSubjectName(const QString &subjectId)
{
    // swapper.py:157-161
    const QJsonObject subject = findSubject(subjectId);
    if (!subject.isEmpty()) {
        return subject.value(QLatin1String("name")).toString();
    }
    return QString();
}

// ── 换课操作 ─────────────────────────────────────────────────

bool ClassSwapManager::swapTwoEntries(const QString &entryIdA, const QString &entryIdB,
                                      int dayOfWeek, int weekOfCycle)
{
    // swapper.py:165-241 swapTwoEntries
    const QJsonObject schedule = m_manager->schedule();
    if (schedule.isEmpty()) {
        return false;
    }
    const int maxCycle = maxWeekCycle(schedule);
    const int applyDayOfWeek = dayOfWeek;
    const int applyWeekOfCycle = weekOfCycle;

    // 记录用户在换课界面的选择
    setSwapPickerContext(dayOfWeek, weekOfCycle);

    // 先把「所选星期+周次」课表整体应用到当前选择时间线，再执行换课
    applyDayScheduleToToday(dayOfWeek, weekOfCycle, applyDayOfWeek, applyWeekOfCycle);

    // 将"基准日课表"的 entry 映射到"今天临时课表"对应位置
    const QString applyEntryIdA = mapEntryToDay(entryIdA, dayOfWeek, weekOfCycle,
                                                applyDayOfWeek, applyWeekOfCycle);
    const QString applyEntryIdB = mapEntryToDay(entryIdB, dayOfWeek, weekOfCycle,
                                                applyDayOfWeek, applyWeekOfCycle);
    if (applyEntryIdA.isEmpty() || applyEntryIdB.isEmpty()) {
        cwn::Log::warn(QStringLiteral("[ClassSwap] Cannot map selected entries to today's "
                                      "temporary schedule"));
        return false;
    }

    // 两个 entry 应用 override 后的真实科目
    const QVariantMap realA = effectiveSubject(entryIdA, dayOfWeek, weekOfCycle, maxCycle);
    const QVariantMap realB = effectiveSubject(entryIdB, dayOfWeek, weekOfCycle, maxCycle);
    if (realA.isEmpty() || realB.isEmpty()) {
        cwn::Log::warn(QStringLiteral("Cannot swap: one of the entries not found"));
        return false;
    }

    const QJsonValue weeksVal = maxCycle > 1 ? QJsonValue(applyWeekOfCycle)
                                             : QJsonValue(QStringLiteral("all"));

    // 作用到"今天"对应位置的临时课表
    setOrUpdateOverride(applyEntryIdA, { applyDayOfWeek }, weeksVal,
                        realB.value(QStringLiteral("subjectId")).toString(),
                        realB.value(QStringLiteral("title")).toString());
    setOrUpdateOverride(applyEntryIdB, { applyDayOfWeek }, weeksVal,
                        realA.value(QStringLiteral("subjectId")).toString(),
                        realA.value(QStringLiteral("title")).toString());

    addSwapRecord(QStringLiteral("swap"), applyEntryIdA, applyEntryIdB,
                  realA.value(QStringLiteral("subjectId")).toString(),
                  realB.value(QStringLiteral("subjectId")).toString());

    emit swapCommitted();
    emit updated();
    return true;
}

bool ClassSwapManager::replaceEntry(const QString &entryId, const QString &newSubjectId,
                                    int dayOfWeek, int weekOfCycle)
{
    // swapper.py:243-291 replaceEntry
    const QJsonObject schedule = m_manager->schedule();
    if (schedule.isEmpty()) {
        return false;
    }
    const int maxCycle = maxWeekCycle(schedule);
    const int applyDayOfWeek = dayOfWeek;
    const int applyWeekOfCycle = weekOfCycle;

    setSwapPickerContext(dayOfWeek, weekOfCycle);

    applyDayScheduleToToday(dayOfWeek, weekOfCycle, applyDayOfWeek, applyWeekOfCycle);

    const QString applyEntryId = mapEntryToDay(entryId, dayOfWeek, weekOfCycle,
                                               applyDayOfWeek, applyWeekOfCycle);
    if (applyEntryId.isEmpty()) {
        cwn::Log::warn(QStringLiteral("[ClassSwap] Cannot map selected entry to today's "
                                      "temporary schedule"));
        return false;
    }

    const QVariantMap oldInfo = effectiveSubject(applyEntryId, applyDayOfWeek,
                                                 applyWeekOfCycle, maxCycle);
    const QString oldSubjectId = oldInfo.isEmpty()
        ? QString() : oldInfo.value(QStringLiteral("subjectId")).toString();

    const QJsonValue weeksVal = maxCycle > 1 ? QJsonValue(applyWeekOfCycle)
                                             : QJsonValue(QStringLiteral("all"));

    setOrUpdateOverride(applyEntryId, { applyDayOfWeek }, weeksVal, newSubjectId, QString());

    addSwapRecord(QStringLiteral("replace"), applyEntryId, QString(), oldSubjectId, newSubjectId);

    emit swapCommitted();
    emit updated();
    return true;
}

// ── 持久化 ───────────────────────────────────────────────────

void ClassSwapManager::saveSwapRecords()
{
    // swapper.py:295-317 saveSwapRecords
    int dayOfWeek = getPreferredDayOfWeek();
    int weekOfCycle = getPreferredWeekOfCycle();
    const QJsonObject currentSwapData = classSwapData(m_configs);
    if (!currentSwapData.isEmpty()) {
        const QJsonValue storedDay = currentSwapData.value(QLatin1String("day_of_week"));
        const QJsonValue storedWeek = currentSwapData.value(QLatin1String("week_of_cycle"));
        if (storedDay.isDouble()) {
            dayOfWeek = storedDay.toInt();
        }
        if (storedWeek.isDouble()) {
            weekOfCycle = storedWeek.toInt();
        }
    }

    QJsonObject swapData;
    swapData.insert(QLatin1String("date"), todayString());
    swapData.insert(QLatin1String("records"), QJsonArray::fromVariantList(m_swapRecords));
    swapData.insert(QLatin1String("day_of_week"), dayOfWeek);
    swapData.insert(QLatin1String("week_of_cycle"), weekOfCycle);
    if (m_configs) {
        m_configs->set(QStringLiteral("schedule.class_swap"), swapData);
    }
    cwn::Log::info(QStringLiteral("Swap records saved: %1 records").arg(m_swapRecords.size()));
}

void ClassSwapManager::loadSwapRecords()
{
    // swapper.py:319-357 loadSwapRecords
    const QJsonObject swapData = classSwapData(m_configs);
    if (swapData.isEmpty()) {
        m_swapRecords.clear();
        m_swapDate.clear();
        return;
    }

    const QJsonValue dayRaw = swapData.value(QLatin1String("day_of_week"));
    int dayOfWeek = dayRaw.isDouble() ? dayRaw.toInt() : getCurrentDayOfWeek();
    if (dayOfWeek < 1 || dayOfWeek > 7) {
        dayOfWeek = getCurrentDayOfWeek();
    }
    const int maxCycle = getMaxWeekCycle();
    const QJsonValue weekRaw = swapData.value(QLatin1String("week_of_cycle"));
    int weekOfCycle = weekRaw.isDouble() ? weekRaw.toInt() : getCurrentWeekOfCycle();
    if (weekOfCycle < 1 || weekOfCycle > maxCycle) {
        weekOfCycle = getCurrentWeekOfCycle();
    }

    const QString savedDate = swapData.value(QLatin1String("date")).toString();
    const QString today = todayString();

    if (savedDate != today) {
        // 跨天，清理临时课表（swapper.py:341-349）
        cwn::Log::info(QStringLiteral("Swap records expired (saved: %1, today: %2), cleaning up")
                           .arg(savedDate, today));
        cleanupSwapOverrides(swapData.value(QLatin1String("records")).toArray().toVariantList());
        m_swapRecords.clear();
        m_swapDate.clear();
        if (m_configs) {
            m_configs->set(QStringLiteral("schedule.class_swap"), QJsonObject());
        }
        return;
    }

    m_swapRecords.clear();
    const QJsonArray records = swapData.value(QLatin1String("records")).toArray();
    for (const QJsonValue &v : records) {
        if (v.isObject()) {
            m_swapRecords.append(normalizeSwapRecord(v.toObject().toVariantMap()));
        }
    }
    m_swapDate = savedDate;
    setSwapPickerContext(dayOfWeek, weekOfCycle);
    saveSwapRecords();
    rebuildOverridesFromRecords(m_swapRecords);
    cwn::Log::info(QStringLiteral("Loaded %1 swap records for today").arg(m_swapRecords.size()));
}

bool ClassSwapManager::hasTodaySwaps()
{
    // swapper.py:359-383 hasTodaySwaps
    const QJsonObject swapData = classSwapData(m_configs);
    if (swapData.isEmpty()) {
        return false;
    }
    // 记录存在
    const QJsonValue records = swapData.value(QLatin1String("records"));
    if (records.isArray() && records.toArray().size() > 0) {
        return true;
    }
    // 仅 picker 上下文（day/week）也视为存在临时课表
    const QJsonValue day = swapData.value(QLatin1String("day_of_week"));
    const QJsonValue week = swapData.value(QLatin1String("week_of_cycle"));
    if (day.isDouble() && week.isDouble()) {
        return true;
    }
    return !m_swapRecords.isEmpty();
}

QVariantList ClassSwapManager::getSwapRecords()
{
    // swapper.py:385-388
    return m_swapRecords;
}

void ClassSwapManager::discardTodaySwaps()
{
    // swapper.py:390-398 discardTodaySwaps
    cleanupSwapOverrides(m_swapRecords);
    m_swapRecords.clear();
    m_swapDate.clear();
    if (m_configs) {
        m_configs->set(QStringLiteral("schedule.class_swap"), QJsonObject());
    }
    emit updated();
    cwn::Log::info(QStringLiteral("All today's swaps discarded"));
}

// ── 内部方法 ─────────────────────────────────────────────────

QJsonObject ClassSwapManager::findSubject(const QString &subjectId)
{
    // swapper.py:402-410 _find_subject（注意：与 ScheduleModel::findSubject 的
    // 二参形式不同，成员函数只按 id 在当前课表中查找）
    const QJsonObject schedule = m_manager->schedule();
    if (schedule.isEmpty() || subjectId.isEmpty()) {
        return {};
    }
    return ScheduleModel::findSubject(ScheduleModel::subjects(schedule), subjectId);
}

QVariantMap ClassSwapManager::effectiveSubject(const QString &entryId, int dayOfWeek,
                                               int weekOfCycle, int maxCycle)
{
    // swapper.py:412-456 _get_effective_subject
    const QJsonObject schedule = m_manager->schedule();
    if (schedule.isEmpty()) {
        return {};
    }

    // 找原始 entry
    QJsonObject entry;
    const QJsonArray dayList = ScheduleModel::days(schedule);
    for (const QJsonValue &v : dayList) {
        const QJsonArray entries = v.toObject().value(QLatin1String("entries")).toArray();
        for (const QJsonValue &ev : entries) {
            if (entryId(ev.toObject()) == entryId) {
                entry = ev.toObject();
                break;
            }
        }
        if (!entry.isEmpty()) {
            break;
        }
    }
    if (entry.isEmpty()) {
        return {};
    }

    QVariantMap data;
    data.insert(QStringLiteral("subjectId"), entrySubjectId(entry));
    data.insert(QStringLiteral("title"), entryTitle(entry));
    data.insert(QStringLiteral("startTime"), entryStartTime(entry));
    data.insert(QStringLiteral("endTime"), entryEndTime(entry));

    // 应用 override（取最高优先级）
    int bestPriority = -1;
    const QJsonArray overrideList = ScheduleModel::overrides(schedule);
    for (const QJsonValue &v : overrideList) {
        const QJsonObject override_ = v.toObject();
        if (overrideEntryId(override_) != entryId) {
            continue;
        }
        const QJsonValue dow = override_.value(QLatin1String("dayOfWeek"));
        if (!dow.isNull() && !dow.isUndefined()
            && !intInDayOfWeek(dow, dayOfWeek)) {
            continue;
        }
        const int priority = overridePriority(override_.value(QLatin1String("weeks")),
                                              weekOfCycle, maxCycle);
        if (priority > bestPriority) {
            bestPriority = priority;
            const QString subject = override_.value(QLatin1String("subjectId")).toString();
            const QString title = override_.value(QLatin1String("title")).toString();
            const QString start = override_.value(QLatin1String("startTime")).toString();
            const QString end = override_.value(QLatin1String("endTime")).toString();
            if (!subject.isEmpty()) {
                data.insert(QStringLiteral("subjectId"), subject);
            }
            if (!title.isEmpty()) {
                data.insert(QStringLiteral("title"), title);
            }
            if (!start.isEmpty()) {
                data.insert(QStringLiteral("startTime"), start);
            }
            if (!end.isEmpty()) {
                data.insert(QStringLiteral("endTime"), end);
            }
        }
    }
    return data;
}

void ClassSwapManager::setOrUpdateOverride(const QString &entryId, const QVariantList &dayOfWeek,
                                           const QJsonValue &weeks, const QString &subjectId,
                                           const QString &title, const QString &startTime,
                                           const QString &endTime)
{
    // swapper.py:468-504 _set_or_update_override
    QJsonObject schedule = m_manager->schedule();
    if (schedule.isEmpty()) {
        return;
    }

    const QJsonValue wantDow = QJsonArray::fromVariantList(dayOfWeek);
    QJsonArray overrideList = ScheduleModel::overrides(schedule);
    for (int i = 0; i < overrideList.size(); ++i) {
        QJsonObject override_ = overrideList.at(i).toObject();
        if (override_.value(QLatin1String("entryId")).toString() != entryId) {
            continue;
        }
        if (override_.value(QLatin1String("dayOfWeek")) != wantDow) {
            continue;
        }
        // 检查 weeks 匹配（swapper.py:482：weeks 相同，或 "all" vs null）
        const QJsonValue oldWeeks = override_.value(QLatin1String("weeks"));
        const bool weeksMatch = oldWeeks == weeks
            || (weeks.isString() && weeks.toString() == QLatin1String("all")
                && oldWeeks.isNull());
        if (!weeksMatch) {
            continue;
        }
        override_.insert(QLatin1String("subjectId"),
                         subjectId.isEmpty() ? QJsonValue(QJsonValue::Null)
                                             : QJsonValue(subjectId));
        override_.insert(QLatin1String("title"),
                         title.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(title));
        if (!startTime.isNull()) {
            override_.insert(QLatin1String("startTime"),
                             startTime.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                 : QJsonValue(startTime));
        }
        if (!endTime.isNull()) {
            override_.insert(QLatin1String("endTime"),
                             endTime.isEmpty() ? QJsonValue(QJsonValue::Null)
                                               : QJsonValue(endTime));
        }
        overrideList[i] = override_;
        schedule.insert(QLatin1String("overrides"), overrideList);
        m_manager->modify(schedule);
        return;
    }

    // 新建 override（swapper.py:493-504）
    QJsonObject override_;
    override_.insert(QLatin1String("id"), generateId(QStringLiteral("swap")));
    override_.insert(QLatin1String("entryId"), entryId);
    override_.insert(QLatin1String("dayOfWeek"), wantDow);
    override_.insert(QLatin1String("weeks"), weeks);
    override_.insert(QLatin1String("subjectId"),
                     subjectId.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(subjectId));
    override_.insert(QLatin1String("title"),
                     title.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(title));
    override_.insert(QLatin1String("startTime"),
                     startTime.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(startTime));
    override_.insert(QLatin1String("endTime"),
                     endTime.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(endTime));
    overrideList.append(override_);
    schedule.insert(QLatin1String("overrides"), overrideList);
    m_manager->modify(schedule);
}

void ClassSwapManager::addSwapRecord(const QString &swapType, const QString &entryA,
                                     const QString &entryB, const QString &oldSubject,
                                     const QString &newSubject)
{
    // swapper.py:506-518 _add_swap_record
    QVariantMap record;
    record.insert(QStringLiteral("type"), swapType);
    record.insert(QStringLiteral("entry_a"), entryA);
    record.insert(QStringLiteral("entry_b"), entryB);
    record.insert(QStringLiteral("old_subject"), oldSubject);
    record.insert(QStringLiteral("new_subject"), newSubject);
    record.insert(QStringLiteral("timestamp"),
                  QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    m_swapRecords.append(record);
    saveSwapRecords();
}

QVariantMap ClassSwapManager::normalizeSwapRecord(const QVariantMap &record)
{
    // swapper.py:520-530 _normalize_swap_record：移除历史 day/week 冗余字段
    QVariantMap normalized;
    normalized.insert(QStringLiteral("type"),
                      record.value(QStringLiteral("type"), QStringLiteral("replace")));
    normalized.insert(QStringLiteral("entry_a"), record.value(QStringLiteral("entry_a"), QString()));
    normalized.insert(QStringLiteral("entry_b"), record.value(QStringLiteral("entry_b"), QString()));
    normalized.insert(QStringLiteral("old_subject"),
                      record.value(QStringLiteral("old_subject"), QString()));
    normalized.insert(QStringLiteral("new_subject"),
                      record.value(QStringLiteral("new_subject"), QString()));
    normalized.insert(QStringLiteral("timestamp"),
                      record.value(QStringLiteral("timestamp"),
                                   QDateTime::currentDateTime().toString(Qt::ISODateWithMs)));
    return normalized;
}

QString ClassSwapManager::mapEntryToDay(const QString &sourceEntryId, int sourceDayOfWeek,
                                        int sourceWeekOfCycle, int targetDayOfWeek,
                                        int targetWeekOfCycle)
{
    // swapper.py:532-555 _map_entry_to_day：按 class/activity 顺序映射到目标日
    const QVariantList sourceEntries = dayEntriesInternal(sourceDayOfWeek, sourceWeekOfCycle, false);
    const QVariantList targetEntries = dayEntriesInternal(targetDayOfWeek, targetWeekOfCycle, false);
    if (sourceEntries.isEmpty() || targetEntries.isEmpty()) {
        return QString();
    }

    int sourceIndex = -1;
    for (int i = 0; i < sourceEntries.size(); ++i) {
        if (sourceEntries.at(i).toMap().value(QStringLiteral("id")).toString() == sourceEntryId) {
            sourceIndex = i;
            break;
        }
    }
    if (sourceIndex < 0 || sourceIndex >= targetEntries.size()) {
        return QString();
    }
    return targetEntries.at(sourceIndex).toMap().value(QStringLiteral("id")).toString();
}

void ClassSwapManager::applyDayScheduleToToday(int sourceDayOfWeek, int sourceWeekOfCycle,
                                               int targetDayOfWeek, int targetWeekOfCycle)
{
    // swapper.py:557-597 _apply_day_schedule_to_today
    const QJsonObject schedule = m_manager->schedule();
    if (schedule.isEmpty()) {
        return;
    }
    const int maxCycle = maxWeekCycle(schedule);
    // swapper.py:570：max_cycle > 1 时用目标周期周，否则 "all"
    const QJsonValue weeksVal = maxCycle > 1 ? QJsonValue(targetWeekOfCycle)
                                             : QJsonValue(QStringLiteral("all"));

    const QVariantList sourceEntries = dayEntriesInternal(sourceDayOfWeek, sourceWeekOfCycle, true);
    const QVariantList targetEntries = dayEntriesInternal(targetDayOfWeek, targetWeekOfCycle, true);
    if (sourceEntries.isEmpty() || targetEntries.isEmpty()) {
        cwn::Log::warn(QStringLiteral("[ClassSwap] apply day schedule failed: source=%1, target=%2")
                           .arg(sourceEntries.size()).arg(targetEntries.size()));
        return;
    }

    const int size = qMin(sourceEntries.size(), targetEntries.size());
    for (int i = 0; i < size; ++i) {
        const QVariantMap src = sourceEntries.at(i).toMap();
        const QVariantMap tgt = targetEntries.at(i).toMap();
        const QString targetEntryId = tgt.value(QStringLiteral("id")).toString();
        if (targetEntryId.isEmpty()) {
            continue;
        }
        setOrUpdateOverride(targetEntryId, { targetDayOfWeek }, weeksVal,
                            src.value(QStringLiteral("subjectId")).toString(),
                            src.value(QStringLiteral("title")).toString(),
                            src.value(QStringLiteral("startTime")).toString(),
                            src.value(QStringLiteral("endTime")).toString());
    }
}

QVariantList ClassSwapManager::dayEntriesInternal(int dayOfWeek, int weekOfCycle,
                                                  bool includeNonClass)
{
    // swapper.py:599-659 _get_day_entries
    const QJsonObject schedule = m_manager->schedule();
    if (schedule.isEmpty()) {
        cwn::Log::warn(QStringLiteral("[ClassSwap] getDayEntries: schedule is None"));
        return {};
    }
    const int maxCycle = maxWeekCycle(schedule);

    const QJsonArray dayList = ScheduleModel::days(schedule);
    for (const QJsonValue &dayValue : dayList) {
        const QJsonObject day = dayValue.toObject();
        const QStringList dowList = dayOfWeekList(day.value(QLatin1String("dayOfWeek")));
        if (dowList.isEmpty() || !dowList.contains(QString::number(dayOfWeek))) {
            continue;
        }
        if (!isInWeek(day.value(QLatin1String("weeks")), weekOfCycle, maxCycle)) {
            continue;
        }

        // 深拷贝 entries 并应用 override（swapper.py:624-639）
        QJsonArray entries = day.value(QLatin1String("entries")).toArray();
        const QJsonArray overrideList = ScheduleModel::overrides(schedule);
        for (int i = 0; i < entries.size(); ++i) {
            QJsonObject entry = entries.at(i).toObject();
            for (const QJsonValue &ov : overrideList) {
                const QJsonObject override_ = ov.toObject();
                if (overrideEntryId(override_) != entryId(entry)) {
                    continue;
                }
                if (!overrideApplies(override_, dayOfWeek, weekOfCycle, maxCycle)) {
                    continue;
                }
                const QString subject = override_.value(QLatin1String("subjectId")).toString();
                const QString title = override_.value(QLatin1String("title")).toString();
                const QString start = override_.value(QLatin1String("startTime")).toString();
                const QString end = override_.value(QLatin1String("endTime")).toString();
                if (!subject.isEmpty()) {
                    entry.insert(QLatin1String("subjectId"), subject);
                }
                if (!title.isEmpty()) {
                    entry.insert(QLatin1String("title"), title);
                }
                if (!start.isEmpty()) {
                    entry.insert(QLatin1String("startTime"), start);
                }
                if (!end.isEmpty()) {
                    entry.insert(QLatin1String("endTime"), end);
                }
            }
            entries[i] = entry;
        }

        // 组装结果（swapper.py:641-654）
        QVariantList result;
        for (const QJsonValue &ev : entries) {
            const QJsonObject entry = ev.toObject();
            if (!includeNonClass) {
                const QString type = entryType(entry);
                if (type != QLatin1String(kTypeClass) && type != QLatin1String(kTypeActivity)) {
                    continue;
                }
            }
            QVariantMap item = entry.toVariantMap();
            const QJsonObject subject = ScheduleModel::findSubject(
                ScheduleModel::subjects(schedule), entrySubjectId(entry));
            item.insert(QStringLiteral("subjectName"),
                        subject.isEmpty() ? entryTitle(entry)
                                          : subject.value(QLatin1String("name")).toString());
            item.insert(QStringLiteral("subjectColor"),
                        subject.isEmpty() ? QString()
                                          : subject.value(QLatin1String("color")).toString());
            item.insert(QStringLiteral("subjectIcon"),
                        subject.isEmpty() ? QString()
                                          : subject.value(QLatin1String("icon")).toString());
            result.append(item);
        }
        return result; // 仅取第一个匹配的时间线
    }

    cwn::Log::warn(QStringLiteral("[ClassSwap] no timeline matched for day=%1, week=%2, "
                                  "max_cycle=%3").arg(dayOfWeek).arg(weekOfCycle).arg(maxCycle));
    return {};
}

void ClassSwapManager::clearTodaySwapOverrides(int dayOfWeek, int weekOfCycle)
{
    // swapper.py:661-677 _clear_today_swap_overrides
    QJsonObject schedule = m_manager->schedule();
    if (schedule.isEmpty()) {
        return;
    }
    const int maxCycle = maxWeekCycle(schedule);
    QJsonArray kept;
    const QJsonArray overrideList = ScheduleModel::overrides(schedule);
    for (const QJsonValue &v : overrideList) {
        const QJsonObject override_ = v.toObject();
        const QString id = override_.value(QLatin1String("id")).toString();
        const bool isSwap = id.startsWith(QStringLiteral("swap_"));
        if (isSwap && overrideApplies(override_, dayOfWeek, weekOfCycle, maxCycle)) {
            continue; // 移除
        }
        kept.append(v);
    }
    if (kept.size() != overrideList.size()) {
        schedule.insert(QLatin1String("overrides"), kept);
        m_manager->modify(schedule);
    }
}

void ClassSwapManager::cleanupSwapOverrides(const QVariantList &records)
{
    // swapper.py:679-693 _cleanup_swap_overrides：清理所有换课产生的 override
    Q_UNUSED(records);
    QJsonObject schedule = m_manager->schedule();
    if (schedule.isEmpty()) {
        return;
    }
    const QJsonArray overrideList = ScheduleModel::overrides(schedule);
    QJsonArray kept;
    int removed = 0;
    for (const QJsonValue &v : overrideList) {
        if (v.toObject().value(QLatin1String("id")).toString().startsWith(QStringLiteral("swap_"))) {
            ++removed;
            continue;
        }
        kept.append(v);
    }
    if (removed > 0) {
        schedule.insert(QLatin1String("overrides"), kept);
        m_manager->modify(schedule);
        cwn::Log::info(QStringLiteral("Cleaned up %1 swap overrides").arg(removed));
    }
}

void ClassSwapManager::rebuildOverridesFromRecords(const QVariantList &records)
{
    // swapper.py:695-751 _rebuild_overrides_from_records：启动后内存恢复
    if (records.isEmpty()) {
        return;
    }
    QJsonObject schedule = m_manager->schedule();
    if (schedule.isEmpty()) {
        return;
    }

    // 先清理已有 swap override，避免重复叠加
    cleanupSwapOverrides(records);

    const int maxCycle = maxWeekCycle(schedule);
    const int applyDayOfWeek = getCurrentDayOfWeek();
    const int applyWeekOfCycle = getCurrentWeekOfCycle();
    const QJsonValue weeksVal = maxCycle > 1 ? QJsonValue(applyWeekOfCycle)
                                             : QJsonValue(QStringLiteral("all"));

    for (const QVariant &recordValue : records) {
        const QVariantMap record = recordValue.toMap();
        const QString swapType = record.value(QStringLiteral("type")).toString();
        if (swapType == QLatin1String("swap")) {
            const QString entryA = record.value(QStringLiteral("entry_a")).toString();
            const QString entryB = record.value(QStringLiteral("entry_b")).toString();
            const QString oldSubject = record.value(QStringLiteral("old_subject")).toString();
            const QString newSubject = record.value(QStringLiteral("new_subject")).toString();
            if (!entryA.isEmpty() && !newSubject.isEmpty()) {
                setOrUpdateOverride(entryA, { applyDayOfWeek }, weeksVal, newSubject, QString());
            }
            if (!entryB.isEmpty() && !oldSubject.isEmpty()) {
                setOrUpdateOverride(entryB, { applyDayOfWeek }, weeksVal, oldSubject, QString());
            }
        } else if (swapType == QLatin1String("replace")) {
            const QString entryId = record.value(QStringLiteral("entry_a")).toString();
            const QString newSubject = record.value(QStringLiteral("new_subject")).toString();
            if (!entryId.isEmpty() && !newSubject.isEmpty()) {
                setOrUpdateOverride(entryId, { applyDayOfWeek }, weeksVal, newSubject, QString());
            }
        }
    }
}
