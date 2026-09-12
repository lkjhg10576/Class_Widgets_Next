#include "ScheduleModel.h"

#include <QCoreApplication>
#include <QUuid>

namespace ScheduleModel {

QString generateId(const QString &prefix)
{
    // 对应 utils/__init__.py:15-16 generate_id：uuid4().hex 为 32 位十六进制
    const QString hex = QUuid::createUuid().toString(QUuid::Id128);
    return prefix + QLatin1Char('_')
         + QString(hex).remove(QLatin1Char('{')).remove(QLatin1Char('}'))
                   .remove(QLatin1Char('-'));
}

int weekNumber(const QString &startDate, const QDate &date)
{
    // utils/calculator.py:4-18 get_week_number
    const QDate start = QDate::fromString(startDate, QStringLiteral("yyyy-MM-dd"));
    if (!start.isValid()) {
        return 1; // service.py:184-186：无有效 startDate 时回退第 1 周
    }
    const qint64 deltaDays = start.daysTo(date);
    if (deltaDays >= 0) {
        return static_cast<int>(deltaDays / 7) + 1;
    }
    // 开学日前：按周继续向前编号（前 1~7 天 → -1，前 8~14 天 → -2）
    return -static_cast<int>(((-deltaDays) + 6) / 7);
}

int cycleWeek(int weekNumber, int cycle)
{
    // utils/calculator.py:20-33 get_cycle_week；cycle 下限保护
    if (cycle < 1) {
        cycle = 1;
    }
    if (weekNumber >= 1) {
        return ((weekNumber - 1) % cycle) + 1;
    }
    // 负周次按 Python 取模语义（-1 % 2 == 1）
    const int pyMod = ((weekNumber % cycle) + cycle) % cycle;
    return pyMod + 1;
}

QTime parseHm(const QString &text)
{
    return QTime::fromString(text, QStringLiteral("HH:mm"));
}

QJsonValue toJsonValue(const QVariant &value)
{
    if (!value.isValid() || value.isNull()) {
        return QJsonValue(QJsonValue::Null);
    }
    return QJsonValue::fromVariant(value);
}

QString dateParamToIso(const QVariant &value)
{
    // QML 可能传 "yyyy-MM-dd" 字符串或 JS Date（DayEditor.qml selectedDate）
    if (value.isNull() || !value.isValid()) {
        return {};
    }
    if (value.canConvert<QDateTime>()) {
        const QDateTime dt = value.toDateTime();
        if (dt.isValid()) {
            return dt.date().toString(QStringLiteral("yyyy-MM-dd"));
        }
    }
    if (value.canConvert<QDate>()) {
        const QDate d = value.toDate();
        if (d.isValid()) {
            return d.toString(QStringLiteral("yyyy-MM-dd"));
        }
    }
    const QString text = value.toString();
    return QDate::fromString(text, QStringLiteral("yyyy-MM-dd")).isValid() ? text : QString();
}

QJsonObject meta(const QJsonObject &schedule)
{
    return schedule.value(QLatin1String("meta")).toObject();
}

QJsonArray subjects(const QJsonObject &schedule)
{
    return schedule.value(QLatin1String("subjects")).toArray();
}

QJsonArray days(const QJsonObject &schedule)
{
    return schedule.value(QLatin1String("days")).toArray();
}

QJsonArray overrides(const QJsonObject &schedule)
{
    return schedule.value(QLatin1String("overrides")).toArray();
}

QString metaId(const QJsonObject &meta)
{
    return meta.value(QLatin1String("id")).toString();
}

int metaVersion(const QJsonObject &meta)
{
    return meta.value(QLatin1String("version")).toInt(kSchemaVersion);
}

int metaMaxWeekCycle(const QJsonObject &meta)
{
    return meta.value(QLatin1String("maxWeekCycle")).toInt(1);
}

QString metaStartDate(const QJsonObject &meta)
{
    return meta.value(QLatin1String("startDate")).toString();
}

int maxWeekCycle(const QJsonObject &schedule)
{
    const int cycle = metaMaxWeekCycle(meta(schedule));
    return cycle >= 1 ? cycle : 1; // service.py:36 `maxWeekCycle or 1`
}

QString startDate(const QJsonObject &schedule)
{
    return metaStartDate(meta(schedule));
}

QString entryId(const QJsonObject &entry)
{
    return entry.value(QLatin1String("id")).toString();
}

QString entryType(const QJsonObject &entry)
{
    return entry.value(QLatin1String("type")).toString();
}

QString entryStartTime(const QJsonObject &entry)
{
    return entry.value(QLatin1String("startTime")).toString();
}

QString entryEndTime(const QJsonObject &entry)
{
    return entry.value(QLatin1String("endTime")).toString();
}

QString entrySubjectId(const QJsonObject &entry)
{
    return entry.value(QLatin1String("subjectId")).toString();
}

QString entryTitle(const QJsonObject &entry)
{
    return entry.value(QLatin1String("title")).toString();
}

QString overrideEntryId(const QJsonObject &override_)
{
    return override_.value(QLatin1String("entryId")).toString();
}

QStringList dayOfWeekList(const QJsonValue &value)
{
    // service.py:51 `[day.dayOfWeek] if isinstance(day.dayOfWeek, int) else day.dayOfWeek`
    if (value.isDouble()) {
        return { QString::number(value.toInt()) };
    }
    QStringList result;
    const QJsonArray array = value.toArray();
    result.reserve(array.size());
    for (const QJsonValue &v : array) {
        if (v.isDouble()) {
            result.append(QString::number(v.toInt()));
        }
    }
    return result;
}

bool isInWeek(const QJsonValue &weeks, int currentWeek, int maxWeekCycle)
{
    // service.py:189-210 _is_in_week
    if (weeks.isNull() || weeks.isUndefined()) {
        return true; // None → 不限制
    }
    if (weeks.isString()) {
        return weeks.toString() == QLatin1String("all"); // WeekType.ALL
    }
    if (weeks.isDouble()) {
        const int start = weeks.toInt();
        return currentWeek >= start && ((currentWeek - start) % maxWeekCycle) == 0;
    }
    if (weeks.isArray()) {
        const QJsonArray list = weeks.toArray();
        for (const QJsonValue &v : list) {
            if (v.isDouble() && v.toInt() == currentWeek) {
                return true;
            }
        }
        return false;
    }
    return false;
}

int overridePriority(const QJsonValue &weeks, int weekOfCycle, int maxWeekCycle)
{
    // swapper.py:458-466 _get_override_priority
    if (weeks.isArray()) {
        const QJsonArray list = weeks.toArray();
        for (const QJsonValue &v : list) {
            if (v.isDouble() && v.toInt() == weekOfCycle) {
                return 3;
            }
        }
        return -1;
    }
    if (weeks.isDouble()) {
        const int start = weeks.toInt();
        if (weekOfCycle >= start && ((weekOfCycle - start) % maxWeekCycle) == 0) {
            return 2;
        }
        return -1;
    }
    if (weeks.isNull() || weeks.isUndefined()
        || weeks.toString() == QLatin1String("all")) {
        return 1;
    }
    return -1;
}

bool overrideApplies(const QJsonObject &override_, int weekday, int currentWeek,
                     int maxWeekCycle)
{
    // service.py:90-97 _override_applies
    const QJsonValue dayOfWeek = override_.value(QLatin1String("dayOfWeek"));
    if (!dayOfWeek.isNull() && !dayOfWeek.isUndefined()) {
        if (!dayOfWeekList(dayOfWeek).contains(QString::number(weekday))) {
            return false;
        }
    }
    const QJsonValue weeks = override_.value(QLatin1String("weeks"));
    if (!weeks.isNull() && !weeks.isUndefined()) {
        if (!isInWeek(weeks, currentWeek, maxWeekCycle)) {
            return false;
        }
    }
    return true;
}

QJsonObject normalizeSubject(QJsonObject subject)
{
    // model.py:20-28 Subject 字段全集
    if (subject.value(QLatin1String("id")).isUndefined()) {
        subject.insert(QLatin1String("id"), QJsonValue(QString()));
    }
    if (subject.value(QLatin1String("name")).isUndefined()) {
        subject.insert(QLatin1String("name"), QJsonValue(QString()));
    }
    if (subject.value(QLatin1String("simplifiedName")).isUndefined()) {
        subject.insert(QLatin1String("simplifiedName"), QJsonValue(QJsonValue::Null));
    }
    if (subject.value(QLatin1String("teacher")).isUndefined()) {
        subject.insert(QLatin1String("teacher"), QJsonValue(QJsonValue::Null));
    }
    if (subject.value(QLatin1String("icon")).isUndefined()) {
        subject.insert(QLatin1String("icon"), QJsonValue(QJsonValue::Null));
    }
    if (subject.value(QLatin1String("color")).isUndefined()) {
        subject.insert(QLatin1String("color"), QJsonValue(QJsonValue::Null));
    }
    if (subject.value(QLatin1String("location")).isUndefined()) {
        subject.insert(QLatin1String("location"), QJsonValue(QJsonValue::Null));
    }
    if (!subject.value(QLatin1String("isLocalClassroom")).isBool()) {
        // 兼容示例 JSON 里 "isLocalClassRoom"（大写 R）的拼写，缺省按 True
        const QJsonValue legacy = subject.value(QLatin1String("isLocalClassRoom"));
        subject.insert(QLatin1String("isLocalClassroom"),
                       legacy.isBool() ? QJsonValue(legacy.toBool()) : QJsonValue(true));
    }
    return subject;
}

QJsonObject normalizeEntry(QJsonObject entry)
{
    // model.py:31-37 Entry 字段全集
    if (entry.value(QLatin1String("id")).isUndefined()) {
        entry.insert(QLatin1String("id"), QJsonValue(QString()));
    }
    if (entry.value(QLatin1String("type")).isUndefined()) {
        entry.insert(QLatin1String("type"), QJsonValue(QString::fromLatin1(kTypeFree)));
    }
    if (entry.value(QLatin1String("startTime")).isUndefined()) {
        entry.insert(QLatin1String("startTime"), QJsonValue(QString()));
    }
    if (entry.value(QLatin1String("endTime")).isUndefined()) {
        entry.insert(QLatin1String("endTime"), QJsonValue(QString()));
    }
    if (entry.value(QLatin1String("subjectId")).isUndefined()
        || entry.value(QLatin1String("subjectId")).toString().isEmpty()) {
        entry.insert(QLatin1String("subjectId"), QJsonValue(QJsonValue::Null));
    }
    if (entry.value(QLatin1String("title")).isUndefined()
        || entry.value(QLatin1String("title")).toString().isEmpty()) {
        entry.insert(QLatin1String("title"), QJsonValue(QJsonValue::Null));
    }
    return entry;
}

QJsonObject normalizeDay(QJsonObject day)
{
    // model.py:40-45 Timeline 字段全集
    if (day.value(QLatin1String("id")).isUndefined()) {
        day.insert(QLatin1String("id"), QJsonValue(QString()));
    }
    QJsonArray entries = day.value(QLatin1String("entries")).toArray();
    for (int i = 0; i < entries.size(); ++i) {
        entries[i] = normalizeEntry(entries.at(i).toObject());
    }
    day.insert(QLatin1String("entries"), entries);
    if (day.value(QLatin1String("dayOfWeek")).isUndefined()) {
        day.insert(QLatin1String("dayOfWeek"), QJsonValue(QJsonValue::Null));
    }
    if (day.value(QLatin1String("weeks")).isUndefined()) {
        day.insert(QLatin1String("weeks"), QJsonValue(QJsonValue::Null));
    }
    if (day.value(QLatin1String("date")).isUndefined()) {
        day.insert(QLatin1String("date"), QJsonValue(QJsonValue::Null));
    }
    return day;
}

QJsonObject normalizeOverride(QJsonObject override_)
{
    // model.py:55-63 Timetable 字段全集
    if (override_.value(QLatin1String("id")).isUndefined()) {
        override_.insert(QLatin1String("id"), QJsonValue(QString()));
    }
    if (override_.value(QLatin1String("entryId")).isUndefined()) {
        override_.insert(QLatin1String("entryId"), QJsonValue(QString()));
    }
    if (override_.value(QLatin1String("dayOfWeek")).isUndefined()) {
        override_.insert(QLatin1String("dayOfWeek"), QJsonValue(QJsonValue::Null));
    }
    if (override_.value(QLatin1String("weeks")).isUndefined()) {
        override_.insert(QLatin1String("weeks"), QJsonValue(QJsonValue::Null));
    }
    if (override_.value(QLatin1String("subjectId")).isUndefined()
        || override_.value(QLatin1String("subjectId")).toString().isEmpty()) {
        override_.insert(QLatin1String("subjectId"), QJsonValue(QJsonValue::Null));
    }
    if (override_.value(QLatin1String("title")).isUndefined()
        || override_.value(QLatin1String("title")).toString().isEmpty()) {
        override_.insert(QLatin1String("title"), QJsonValue(QJsonValue::Null));
    }
    if (override_.value(QLatin1String("startTime")).isUndefined()
        || override_.value(QLatin1String("startTime")).toString().isEmpty()) {
        override_.insert(QLatin1String("startTime"), QJsonValue(QJsonValue::Null));
    }
    if (override_.value(QLatin1String("endTime")).isUndefined()
        || override_.value(QLatin1String("endTime")).toString().isEmpty()) {
        override_.insert(QLatin1String("endTime"), QJsonValue(QJsonValue::Null));
    }
    return override_;
}

QJsonObject normalizeMeta(QJsonObject meta)
{
    // model.py:48-52 MetaInfo 字段全集
    if (meta.value(QLatin1String("id")).isUndefined()) {
        meta.insert(QLatin1String("id"), QJsonValue(QString()));
    }
    if (!meta.value(QLatin1String("version")).isDouble()) {
        meta.insert(QLatin1String("version"), QJsonValue(kSchemaVersion));
    }
    if (!meta.value(QLatin1String("maxWeekCycle")).isDouble()) {
        meta.insert(QLatin1String("maxWeekCycle"), QJsonValue(1));
    }
    if (meta.value(QLatin1String("startDate")).isUndefined()) {
        meta.insert(QLatin1String("startDate"), QJsonValue(QString()));
    }
    return meta;
}

QJsonObject normalizeSchedule(QJsonObject schedule)
{
    schedule.insert(QLatin1String("meta"), normalizeMeta(schedule.value(QLatin1String("meta")).toObject()));

    QJsonArray subjectList;
    for (const QJsonValue &v : subjects(schedule)) {
        subjectList.append(normalizeSubject(v.toObject()));
    }
    schedule.insert(QLatin1String("subjects"), subjectList);

    QJsonArray dayList;
    for (const QJsonValue &v : days(schedule)) {
        dayList.append(normalizeDay(v.toObject()));
    }
    schedule.insert(QLatin1String("days"), dayList);

    QJsonArray overrideList;
    for (const QJsonValue &v : overrides(schedule)) {
        overrideList.append(normalizeOverride(v.toObject()));
    }
    schedule.insert(QLatin1String("overrides"), overrideList);
    return schedule;
}

QJsonArray defaultSubjects()
{
    // utils/subjects.py:5-29 DEFAULT_SUBJECTS / get_default_subjects
    struct DefaultSubject
    {
        const char *id;
        const char *name;
        const char *simplified;
        const char *icon;
        const char *color;
        bool local;
    };
    static const DefaultSubject kDefaults[] = {
        { "chinese", "Chinese", "CHN", "ic_fluent_book_20_regular", "#FF5722", true },
        { "math", "Mathematics", "Math", "ic_fluent_ruler_20_regular", "#3F51B5", true },
        { "english", "English", "Eng", "ic_fluent_text_list_abc_uppercase_ltr_20_filled", "#2196F3", true },
        { "politics", "Politics", "Civics", "ic_fluent_book_globe_20_regular", "#9C27B0", true },
        { "history", "History", "Hist", "ic_fluent_clock_20_regular", "#795548", true },
        { "physics", "Physics", "Phys", "ic_fluent_lightbulb_filament_20_regular", "#00BCD4", true },
        { "chemistry", "Chemistry", "Chem", "ic_fluent_hexagon_three_20_regular", "#4CAF50", true },
        { "biology", "Biology", "Bio", "ic_fluent_leaf_three_20_regular", "#8BC34A", true },
        { "geography", "Geography", "Geo", "ic_fluent_earth_20_regular", "#009688", true },
        { "music", "Music", "Mus", "ic_fluent_music_note_2_20_regular", "#E91E63", true },
        { "art", "Art", "Art", "ic_fluent_draw_shape_20_regular", "#F44336", true },
        { "psychology", "Psychology", "Psy", "ic_fluent_brain_sparkle_20_regular", "#FF9800", true },
        { "pe", "Physical Education", "PE", "ic_fluent_person_running_20_regular", "#CDDC39", false },
        { "it", "Information Technology", "IT", "ic_fluent_laptop_20_regular", "#607D8B", true },
        { "generaltech", "General Technology", "GenTech", "ic_fluent_wrench_settings_20_regular", "#FF9800", true },
        { "elective", "Elective", "Elective", "ic_fluent_sign_out_20_regular", "#9E9E9E", false },
        { "selfstudy", "Self Study", "Study", "ic_fluent_notebook_20_regular", "#607D8B", true },
        { "club", "Club", "Club", "ic_fluent_people_team_20_regular", "#673AB7", true },
        { "classmeeting", "Class Meeting", "Meeting", "ic_fluent_chat_20_regular", "#3F51B5", true },
        { "weeklytest", "Weekly Test", "Test", "ic_fluent_clipboard_20_regular", "#FF5722", true },
    };

    QJsonArray result;
    for (const DefaultSubject &s : kDefaults) {
        QJsonObject subject;
        subject.insert(QLatin1String("id"), QString::fromLatin1(s.id));
        subject.insert(QLatin1String("name"),
                       QCoreApplication::translate("Subjects", s.name));
        subject.insert(QLatin1String("simplifiedName"),
                       QCoreApplication::translate("SubjectsSimplified", s.simplified));
        subject.insert(QLatin1String("teacher"), QJsonValue(QJsonValue::Null));
        subject.insert(QLatin1String("icon"), QString::fromLatin1(s.icon));
        subject.insert(QLatin1String("color"), QString::fromLatin1(s.color));
        subject.insert(QLatin1String("location"), QJsonValue(QJsonValue::Null));
        subject.insert(QLatin1String("isLocalClassroom"), s.local);
        result.append(subject);
    }
    return result;
}

QJsonObject makeEmptySchedule()
{
    // manager.py:21-30 _create_empty_schedule
    const QDate today = QDate::currentDate();
    QJsonObject meta;
    meta.insert(QLatin1String("id"), generateId(QStringLiteral("meta")));
    meta.insert(QLatin1String("version"), kSchemaVersion);
    meta.insert(QLatin1String("maxWeekCycle"), 2);
    meta.insert(QLatin1String("startDate"),
                QStringLiteral("%1-09-01").arg(today.year()));

    QJsonObject schedule;
    schedule.insert(QLatin1String("meta"), meta);
    schedule.insert(QLatin1String("subjects"), defaultSubjects());
    schedule.insert(QLatin1String("days"), QJsonArray());
    schedule.insert(QLatin1String("overrides"), QJsonArray()); // model.py:70 默认空列表
    return schedule;
}

QJsonObject findSubject(const QJsonArray &subjects, const QString &subjectId)
{
    // service.py:170-177 get_subject
    if (subjectId.isEmpty()) {
        return {};
    }
    for (const QJsonValue &v : subjects) {
        const QJsonObject subject = v.toObject();
        if (subject.value(QLatin1String("id")).toString() == subjectId) {
            return subject;
        }
    }
    return {};
}

} // namespace ScheduleModel
