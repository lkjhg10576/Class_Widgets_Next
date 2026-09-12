#include "ScheduleRuntime.h"

#include "../ConfigStore.h"
#include "../Logger.h"
#include "ScheduleManager.h"
#include "ScheduleModel.h"
#include "UnionTimer.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>

using namespace ScheduleModel;

namespace {

// ─────────────────────────────────────────────────────────────
// 以下为 core/schedule/service.py ScheduleServices 的纯函数移植。
// 上游从 app_central.configs 读取的参数（reschedule_day / class_swap /
// preparation_time）改为显式入参。
// ─────────────────────────────────────────────────────────────

bool isDisplayableType(const QString &type)
{
    // service.py:120 / 131：仅 class 与 activity 参与展示
    return type == QLatin1String(kTypeClass) || type == QLatin1String(kTypeActivity);
}

// service.py:99-111 get_current_entry
QJsonObject getCurrentEntry(const QJsonObject &day, const QDateTime &now)
{
    const QTime nowTime = now.time();
    const QJsonArray entries = day.value(QLatin1String("entries")).toArray();
    for (const QJsonValue &v : entries) {
        const QJsonObject entry = v.toObject();
        const QTime start = parseHm(entryStartTime(entry));
        const QTime end = parseHm(entryEndTime(entry));
        if (!start.isValid() || !end.isValid()) {
            continue; // service.py:107 except ValueError: continue
        }
        if (start <= nowTime && nowTime < end) {
            return entry;
        }
    }
    return {};
}

// service.py:113-122 get_all_entries：当天所有可显示条目，按开始时间排序。
// 上游仅在 runtime.py:265 赋值给 all_entries（无任何消费者，也不对外暴露），
// 此处同样不参与每秒刷新，保留实现以对齐 service.py 全量语义。
QJsonArray getAllEntries(const QJsonObject &day)
{
    QList<QJsonObject> list;
    const QJsonArray entries = day.value(QLatin1String("entries")).toArray();
    for (const QJsonValue &v : entries) {
        const QJsonObject entry = v.toObject();
        if (isDisplayableType(entryType(entry))) {
            list.append(entry);
        }
    }
    std::stable_sort(list.begin(), list.end(), [](const QJsonObject &a, const QJsonObject &b) {
        return parseHm(entryStartTime(a)) < parseHm(entryStartTime(b));
    });
    QJsonArray result;
    for (const QJsonObject &entry : list) {
        result.append(entry);
    }
    return result;
}

// service.py:124-133 get_next_entries：开始时间晚于当前的可显示条目，按开始时间排序
QJsonArray getNextEntries(const QJsonObject &day, const QDateTime &now)
{
    const QTime nowTime = now.time();
    QList<QJsonObject> list;
    const QJsonArray entries = day.value(QLatin1String("entries")).toArray();
    for (const QJsonValue &v : entries) {
        const QJsonObject entry = v.toObject();
        const QTime start = parseHm(entryStartTime(entry));
        if (!start.isValid()) {
            continue; // 上游 strptime 失败会抛异常；这里保守跳过坏数据
        }
        if (start > nowTime && isDisplayableType(entryType(entry))) {
            list.append(entry);
        }
    }
    std::stable_sort(list.begin(), list.end(), [](const QJsonObject &a, const QJsonObject &b) {
        return parseHm(entryStartTime(a)) < parseHm(entryStartTime(b));
    });
    QJsonArray result;
    for (const QJsonObject &entry : list) {
        result.append(entry);
    }
    return result;
}

// 把当天时刻归一到 now 的日期（对应 datetime.combine(now.date(), time)，本地时间）
QDateTime sameDayAt(const QDateTime &now, const QTime &time)
{
    return QDateTime(now.date(), time);
}

// service.py:135-148 get_remaining_time（返回秒数）
qint64 getRemainingSeconds(const QJsonObject &day, const QDateTime &now)
{
    const QJsonObject current = getCurrentEntry(day, now);
    if (!current.isEmpty()) {
        const QTime end = parseHm(entryEndTime(current));
        if (!end.isValid()) {
            return 0;
        }
        const QDateTime endDt = sameDayAt(now, QTime(end.hour(), end.minute(), 0, 0));
        return qMax<qint64>(0, now.secsTo(endDt));
    }
    const QJsonArray upcoming = getNextEntries(day, now);
    if (!upcoming.isEmpty()) {
        const QTime nextStart = parseHm(entryStartTime(upcoming.at(0).toObject()));
        if (!nextStart.isValid()) {
            return 0;
        }
        const QDateTime nextDt = sameDayAt(now, QTime(nextStart.hour(), nextStart.minute(), 0, 0));
        return qMax<qint64>(0, now.secsTo(nextDt));
    }
    return 0;
}

// service.py:150-159 get_current_status（prep_min：预备铃分钟数）
QString getCurrentStatus(const QJsonObject &day, const QDateTime &now, int prepMin)
{
    const QJsonArray upcoming = getNextEntries(day, now);
    if (!upcoming.isEmpty()) {
        const QTime nextStart = parseHm(entryStartTime(upcoming.at(0).toObject()));
        if (nextStart.isValid()) {
            const QDateTime nextDt = sameDayAt(now, nextStart).addSecs(-prepMin * 60);
            // now.replace(microsecond=0)：按秒比较
            const QDateTime nowTruncated = sameDayAt(now, QTime(now.time().hour(),
                                                                now.time().minute(),
                                                                now.time().second(), 0));
            if (nextDt <= nowTruncated) {
                return QString::fromLatin1(kTypePreparation);
            }
        }
    }
    const QJsonObject current = getCurrentEntry(day, now);
    if (!current.isEmpty()) {
        return entryType(current); // service.py:159 current.type
    }
    return QString::fromLatin1(kTypeFree);
}

// service.py:161-168 get_current_subject
QJsonObject getCurrentSubject(const QJsonObject &day, const QJsonArray &subjects,
                              const QDateTime &now)
{
    const QJsonObject current = getCurrentEntry(day, now);
    if (!current.isEmpty()) {
        const QString subjectId = entrySubjectId(current);
        if (!subjectId.isEmpty()) {
            return findSubject(subjects, subjectId);
        }
    }
    return {};
}

// service.py:15-88 get_day_entries：当前日期对应的 Timeline（应用 override 的副本）
QJsonObject getDayEntries(const QJsonObject &schedule, const QDateTime &now,
                          const QJsonObject &rescheduleMap, const QJsonObject &classSwap)
{
    const QDate date = now.date();
    const QString dateStr = date.toString(QStringLiteral("yyyy-MM-dd"));

    // 指定日期时间线优先，不受 dayOfWeek/weeks 限制（service.py:24）
    QJsonObject matchedDay;
    const QJsonArray dayList = days(schedule);
    for (const QJsonValue &v : dayList) {
        const QJsonObject day = v.toObject();
        if (day.value(QLatin1String("date")).toString() == dateStr) {
            matchedDay = day;
            break;
        }
    }

    // 当前是第几周（可为负；service.py:180-187 _get_week_index）
    const QString startStr = startDate(schedule);
    const int rawWeekIndex = startStr.isEmpty() ? 1 : weekNumber(startStr, date);
    const int maxWeekCycleValue = ScheduleModel::maxWeekCycle(schedule);

    // 调休处理：优先使用调休映射表（service.py:29-34）
    int weekday = date.dayOfWeek();
    const QJsonValue rescheduled = rescheduleMap.value(dateStr);
    if (rescheduled.isDouble()) {
        weekday = rescheduled.toInt(weekday);
    }

    int currentWeek = cycleWeek(rawWeekIndex, maxWeekCycleValue); // service.py:36-37

    // 临时换课（service.py:40-47）
    if (classSwap.value(QLatin1String("date")).toString() == dateStr) {
        const QJsonValue swapWeekday = classSwap.value(QLatin1String("day_of_week"));
        const QJsonValue swapWeek = classSwap.value(QLatin1String("week_of_cycle"));
        if (swapWeekday.isDouble() && swapWeekday.toInt() >= 1 && swapWeekday.toInt() <= 7) {
            weekday = swapWeekday.toInt();
        }
        if (swapWeek.isDouble() && swapWeek.toInt() >= 1 && swapWeek.toInt() <= maxWeekCycleValue) {
            currentWeek = swapWeek.toInt();
        }
    }

    // 按星期与周次匹配（service.py:49-58）
    if (matchedDay.isEmpty()) {
        for (const QJsonValue &v : dayList) {
            const QJsonObject day = v.toObject();
            const QStringList dowList = dayOfWeekList(day.value(QLatin1String("dayOfWeek")));
            if (!dowList.isEmpty()
                && dowList.contains(QString::number(weekday))
                && isInWeek(day.value(QLatin1String("weeks")), currentWeek, maxWeekCycleValue)) {
                matchedDay = day;
                break;
            }
        }
    }

    if (matchedDay.isEmpty()) {
        return {}; // service.py:88 → None
    }

    // 深拷贝并应用 override（service.py:60-87）
    QJsonObject dayCopy = matchedDay;
    QJsonArray entries = matchedDay.value(QLatin1String("entries")).toArray();
    const QJsonArray overrideList = overrides(schedule);
    for (int i = 0; i < entries.size(); ++i) {
        QJsonObject entry = entries.at(i).toObject();
        bool subjectOverridden = false;
        bool titleOverridden = false;
        for (const QJsonValue &ov : overrideList) {
            const QJsonObject override_ = ov.toObject();
            if (overrideEntryId(override_) != entryId(entry)) {
                continue;
            }
            if (!overrideApplies(override_, weekday, currentWeek, maxWeekCycleValue)) {
                continue;
            }
            const QString overrideSubject = override_.value(QLatin1String("subjectId")).toString();
            const QString overrideTitle = override_.value(QLatin1String("title")).toString();
            const QString overrideStart = override_.value(QLatin1String("startTime")).toString();
            const QString overrideEnd = override_.value(QLatin1String("endTime")).toString();
            if (!overrideSubject.isEmpty()) {
                entry.insert(QLatin1String("subjectId"), overrideSubject);
                subjectOverridden = true;
            }
            if (!overrideTitle.isEmpty()) {
                entry.insert(QLatin1String("title"), overrideTitle);
                titleOverridden = true;
            }
            if (!overrideStart.isEmpty()) {
                entry.insert(QLatin1String("startTime"), overrideStart);
            }
            if (!overrideEnd.isEmpty()) {
                entry.insert(QLatin1String("endTime"), overrideEnd);
            }
        }
        // 科目被替换且无自定义标题 → 清除时间线的隐式标签（service.py:84-85）
        if (subjectOverridden && !titleOverridden) {
            entry.insert(QLatin1String("title"), QJsonValue(QJsonValue::Null));
        }
        entries[i] = normalizeEntry(entry);
    }
    dayCopy.insert(QLatin1String("entries"), entries);
    return dayCopy;
}

QJsonValue configValue(const ConfigStore *configs, const QString &key)
{
    if (!configs) {
        return QJsonValue(QJsonValue::Undefined);
    }
    const std::optional<QJsonValue> value = configs->value(key);
    return value ? *value : QJsonValue(QJsonValue::Undefined);
}

} // namespace

ScheduleRuntime::ScheduleRuntime(ConfigStore *configs, ScheduleManager *manager,
                                 QObject *parent)
    : QObject(parent)
    , m_configs(configs)
    , m_manager(manager)
{
    m_currentTime = QDateTime::currentDateTime();
    m_currentOffsetTime = m_currentTime;
    m_currentStatus = QString::fromLatin1(kTypeFree); // runtime.py:216 空状态按 free 对外

    // runtime.py:28-31：50ms 单发定时器，合并编辑器连续修改
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(50);
    connect(&m_refreshTimer, &QTimer::timeout, this, [this]() {
        const QJsonObject pending = m_pendingSchedule;
        m_pendingSchedule = QJsonObject();
        if (!pending.isEmpty()) {
            refreshWith(pending);
        }
    });

    // central.py:458：manager.scheduleModified → runtime.schedule_refresh
    if (m_manager) {
        connect(m_manager, &ScheduleManager::scheduleModified,
                this, &ScheduleRuntime::scheduleRefresh);
    }
    // central.py:456 → central.update() → runtime.refresh()：秒级心跳
    connect(&UnionTimer::instance(), &UnionTimer::tick, this, &ScheduleRuntime::refresh);
}

QVariantMap ScheduleRuntime::currentDate() const
{
    // runtime.py:149-151
    const QDate date = m_currentTime.date();
    return { { QStringLiteral("year"), date.year() },
             { QStringLiteral("month"), date.month() },
             { QStringLiteral("day"), date.day() } };
}

QVariantList ScheduleRuntime::subjects() const
{
    // runtime.py:162-166
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

QVariantMap ScheduleRuntime::scheduleMeta() const
{
    // runtime.py:168-172
    if (m_schedule.isEmpty()) {
        return {};
    }
    return meta(m_schedule).toVariantMap();
}

QVariantList ScheduleRuntime::currentDayEntries() const
{
    // runtime.py:174-178：当前日程（全部条目）
    if (m_currentDay.isEmpty()) {
        return {};
    }
    QVariantList result;
    const QJsonArray entries = m_currentDay.value(QLatin1String("entries")).toArray();
    result.reserve(entries.size());
    for (const QJsonValue &v : entries) {
        result.append(v.toObject().toVariantMap());
    }
    return result;
}

QVariantMap ScheduleRuntime::currentEntry() const
{
    // runtime.py:180-182
    return m_currentEntry.toVariantMap();
}

QVariantList ScheduleRuntime::nextEntries() const
{
    // runtime.py:184-188
    return m_nextEntries;
}

QVariantMap ScheduleRuntime::remainingTime() const
{
    // runtime.py:194-205
    if (m_remainingSeconds < 0) {
        return { { QStringLiteral("minute"), 0 }, { QStringLiteral("second"), 0 } };
    }
    return { { QStringLiteral("minute"), static_cast<int>(m_remainingSeconds / 60) },
             { QStringLiteral("second"), static_cast<int>(m_remainingSeconds % 60) } };
}

QString ScheduleRuntime::currentStatus() const
{
    // runtime.py:213-217：空状态对外表现为 free
    if (m_currentStatus.isEmpty()) {
        return QString::fromLatin1(kTypeFree);
    }
    return m_currentStatus;
}

QVariantMap ScheduleRuntime::currentSubject() const
{
    // runtime.py:220-222
    return m_currentSubject.toVariantMap();
}

void ScheduleRuntime::refresh()
{
    // runtime.py:228-237（schedule=None 分支）
    if (m_schedule.isEmpty()) {
        return;
    }
    refreshWith(m_schedule);
}

void ScheduleRuntime::refreshWith(const QJsonObject &schedule)
{
    // runtime.py:228-237 refresh(schedule)
    m_refreshTimer.stop();
    updateSchedule(schedule);
    updateTime();
    updateNotify();
    emit updated();
}

void ScheduleRuntime::scheduleRefresh(const QJsonObject &schedule)
{
    // runtime.py:239-242：合并连续的课表编辑通知，避免拖动时重复重算
    m_pendingSchedule = schedule;
    m_refreshTimer.start();
}

void ScheduleRuntime::updateSchedule(const QJsonObject &schedule)
{
    // runtime.py:250-283 _update_schedule
    if (!schedule.isEmpty()) {
        m_schedule = schedule;
    }

    m_timeOffset = configValue(m_configs, QStringLiteral("schedule.time_offset")).toInt(0);
    m_currentTime = QDateTime::currentDateTime();
    m_currentOffsetTime = m_currentTime.addSecs(m_timeOffset); // 内部计算时间

    const QJsonObject rescheduleMap =
        configValue(m_configs, QStringLiteral("schedule.reschedule_day")).toObject();
    const QJsonObject classSwap =
        configValue(m_configs, QStringLiteral("schedule.class_swap")).toObject();

    m_currentDay = getDayEntries(m_schedule, m_currentOffsetTime, rescheduleMap, classSwap);

    if (!m_currentDay.isEmpty()) {
        m_currentEntry = getCurrentEntry(m_currentDay, m_currentOffsetTime);
        const QJsonArray next = getNextEntries(m_currentDay, m_currentOffsetTime);
        m_nextEntries.clear();
        m_nextEntries.reserve(next.size());
        for (const QJsonValue &v : next) {
            m_nextEntries.append(v.toObject().toVariantMap());
        }
        m_remainingSeconds = getRemainingSeconds(m_currentDay, m_currentOffsetTime);

        // runtime.py:268：preparation_time（getattr(...) or 2 → 0/负值视为 2）
        int prepMin = configValue(m_configs, QStringLiteral("schedule.preparation_time")).toInt(2);
        if (prepMin <= 0) {
            prepMin = 2;
        }
        m_currentStatus = getCurrentStatus(m_currentDay, m_currentOffsetTime, prepMin);

        const QJsonArray subjectList = ScheduleModel::subjects(m_schedule);
        m_currentSubject = getCurrentSubject(m_currentDay, subjectList, m_currentOffsetTime);
        m_currentTitle = entryTitle(m_currentEntry); // runtime.py:271 getattr(title, None)
    } else {
        // runtime.py:272-279
        m_currentEntry = QJsonObject();
        m_nextEntries.clear();
        m_remainingSeconds = -1;
        m_currentStatus = QString::fromLatin1(kTypeFree);
        m_currentSubject = QJsonObject();
        m_currentTitle.clear();
    }

    m_progress = progressPercent();
    if (m_previousEntry != m_currentEntry) {
        emit currentsChanged(m_currentStatus); // runtime.py:282-283
    }
}

void ScheduleRuntime::updateTime()
{
    // runtime.py:285-288 _update_time
    if (m_schedule.isEmpty()) {
        return;
    }
    m_currentDayOfWeek = m_currentOffsetTime.date().dayOfWeek();
    m_currentWeek = weekNumber(startDate(m_schedule), m_currentOffsetTime.date());
    m_currentWeekOfCycle =
        cycleWeek(m_currentWeek, maxWeekCycle(m_schedule));
}

double ScheduleRuntime::progressPercent() const
{
    // runtime.py:290-300 get_progress_percent
    if (m_currentEntry.isEmpty()) {
        return 1;
    }
    const QDateTime now = m_currentOffsetTime;
    const QTime start = parseHm(entryStartTime(m_currentEntry));
    const QTime end = parseHm(entryEndTime(m_currentEntry));
    if (!start.isValid() || !end.isValid()) {
        return 1;
    }
    const QDateTime startDt = sameDayAt(now, start);
    const QDateTime endDt = sameDayAt(now, end);
    if (now <= startDt) {
        return 0;
    }
    if (now >= endDt) {
        return 1;
    }
    const double ratio = static_cast<double>(startDt.secsTo(now))
                       / static_cast<double>(startDt.secsTo(endDt));
    return std::round(ratio * 100.0) / 100.0; // python round(x, 2)
}

void ScheduleRuntime::updateNotify()
{
    // runtime.py:302-443 _update_notify。
    // 通知分发依赖 NotificationProvider 体系（M4 落地，NotificationStub 尚无
    // dispatch 能力），此处仅维护 previous_entry 并记录状态变化日志；
    // 预备铃（runtime.py:445-496）一并延后。
    if (m_previousEntry != m_currentEntry) {
        m_previousEntry = m_currentEntry;
        cwn::Log::info(QStringLiteral("Schedule status changed: %1").arg(currentStatus()));
    }
}
