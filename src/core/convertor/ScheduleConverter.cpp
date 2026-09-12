#include "ScheduleConverter.h"

#include "../Logger.h"
#include "../schedule/ScheduleModel.h"

#include <QDate>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLocale>
#include <QList>
#include <QMap>
#include <QStringList>
#include <QCoreApplication>

#include <algorithm>
#include <cmath>
#include <utility>

// 对应上游 core/convertor/converter.py 的实现。各函数前的行号均指该文件。
// 转换语义（字段映射、默认值、错误处理）逐条对照 converter.py；上游抛异常
// 的路径在此全部折算为"返回 false + *error"，由调用方决定返回值语义。
namespace ScheduleConverter {

namespace {

// ── 小工具 ───────────────────────────────────────────────────────────────

// Python 风格取模（-1 % 2 == 1），converter.py 的奇偶判断依赖该语义
int pythonMod(int value, int base)
{
    const int r = value % base;
    return r < 0 ? r + base : r;
}

// Python int(值) 的宽松语义：bool→0/1，浮点向零截断，字符串允许可选正负号
// 加纯数字；其余（null/容器/非法字符串）视为失败（上游抛 ValueError/TypeError）
bool jsonToInt(const QJsonValue &value, int *out)
{
    switch (value.type()) {
    case QJsonValue::Bool:
        *out = value.toBool() ? 1 : 0;
        return true;
    case QJsonValue::Double:
        *out = static_cast<int>(value.toDouble()); // int(40.9) → 40
        return true;
    case QJsonValue::String: {
        const QString text = value.toString().trimmed();
        int pos = 0;
        bool negative = false;
        if (!text.isEmpty() && (text.at(0) == QLatin1Char('+') || text.at(0) == QLatin1Char('-'))) {
            negative = text.at(0) == QLatin1Char('-');
            pos = 1;
        }
        if (pos >= text.size() || text.size() - pos > 9)
            return false; // 位数保护（int() 支持任意精度，这里超出即视为失败）
        for (int i = pos; i < text.size(); ++i) {
            if (!text.at(i).isDigit())
                return false;
        }
        *out = text.mid(pos).toInt() * (negative ? -1 : 1);
        return true;
    }
    default:
        return false;
    }
}

bool stringToInt(const QString &text, int *out)
{
    return jsonToInt(QJsonValue(text), out);
}

// Python str(值) 的课表名语义（converter.py:235 str(subject_name)、292 等）。
// null 不应走到这里（调用前先做真值判断）；容器视为失败（上游是 repr，无意义）。
bool cw1NameString(const QJsonValue &value, QString *out)
{
    switch (value.type()) {
    case QJsonValue::String:
        *out = value.toString();
        return true;
    case QJsonValue::Double: {
        const double d = value.toDouble();
        if (d == static_cast<double>(static_cast<qint64>(d)))
            *out = QString::number(static_cast<qint64>(d)); // str(5) → "5"
        else
            *out = QString::number(d);                      // str(5.5) → "5.5"
        return true;
    }
    case QJsonValue::Bool:
        *out = value.toBool() ? QStringLiteral("True") : QStringLiteral("False");
        return true;
    default:
        return false;
    }
}

// converter.py:251-256 _is_meaningful_cw1_subject_name：
// 真值判断后 str(...).strip() 不属于 {"", "未添加"}
bool isMeaningfulCw1SubjectName(const QJsonValue &value)
{
    switch (value.type()) {
    case QJsonValue::String: {
        const QString trimmed = value.toString().trimmed();
        return !trimmed.isEmpty() && trimmed != QStringLiteral("未添加");
    }
    case QJsonValue::Double:
        return value.toDouble() != 0.0; // 0 为假值；其余按 str(值) 判断
    case QJsonValue::Bool:
        return value.toBool();
    default:
        return false; // null / 容器（Python：None 为假值，容器 repr 视为无效）
    }
}

// 错误信息里展示原始值（QJsonValue 无内置 toString，按类型给出紧凑形式）
QString describeValue(const QJsonValue &value)
{
    switch (value.type()) {
    case QJsonValue::String:
        return value.toString();
    case QJsonValue::Double:
        return QString::number(value.toDouble());
    case QJsonValue::Bool:
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QJsonValue::Array:
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    case QJsonValue::Object:
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    case QJsonValue::Undefined:
        return QStringLiteral("undefined");
    case QJsonValue::Null:
        break;
    }
    return QStringLiteral("null");
}

// converter.py:163-167 _minutes_to_hhmm：f"{hours:02d}:{minutes:02d}"，
// 采用 Python 的地板除/取模语义（负数分钟仅出现在畸形数据中）
QString minutesToHhmm(int totalMinutes)
{
    qint64 hours = totalMinutes / 60;
    qint64 minutes = totalMinutes % 60;
    if (minutes < 0) {
        minutes += 60;
        hours -= 1;
    }
    return QStringLiteral("%1:%2")
        .arg(static_cast<int>(hours), 2, 10, QLatin1Char('0'))
        .arg(static_cast<int>(minutes), 2, 10, QLatin1Char('0'));
}

// converter.py:150-161 _to_cw_time：字符串 "HH:MM:SS" 或当日秒数 → "HH:MM"
bool toCwTime(const QJsonValue &value, QString *out, QString *error)
{
    switch (value.type()) {
    case QJsonValue::String: {
        // datetime.strptime(str(time), '%H:%M:%S')：严格三段数字
        const QString text = value.toString();
        const QStringList parts = text.split(QLatin1Char(':'));
        if (parts.size() != 3) {
            *error = QStringLiteral("Invalid time value: %1").arg(describeValue(value));
            return false;
        }
        int numbers[3];
        for (int i = 0; i < 3; ++i) {
            const QString &part = parts.at(i);
            if (part.isEmpty()) {
                *error = QStringLiteral("Invalid time value: %1").arg(describeValue(value));
                return false;
            }
            for (const QChar &c : part) {
                if (!c.isDigit()) {
                    *error = QStringLiteral("Invalid time value: %1").arg(describeValue(value));
                    return false;
                }
            }
            numbers[i] = part.toInt();
        }
        if (numbers[0] < 0 || numbers[0] > 23 || numbers[1] < 0 || numbers[1] > 59
            || numbers[2] < 0 || numbers[2] > 59) {
            *error = QStringLiteral("Invalid time value: %1").arg(describeValue(value));
            return false;
        }
        *out = QStringLiteral("%1:%2")
                   .arg(numbers[0], 2, 10, QLatin1Char('0'))
                   .arg(numbers[1], 2, 10, QLatin1Char('0')); // strftime("%H:%M")
        return true;
    }
    case QJsonValue::Double: {
        // converter.py:156-157：f'{int(t/3600)}:{int(t/60%60)}:{t%60}'
        const double d = value.toDouble();
        if (d < 0 || d != std::floor(d)) {
            // 负数或带小数会在 Python 端拼出 strptime 无法解析的字符串 → 报错
            *error = QStringLiteral("Invalid time value: %1").arg(describeValue(value));
            return false;
        }
        const int t = static_cast<int>(d);
        const int hour = t / 3600;
        const int minute = (t / 60) % 60;
        const int second = t % 60;
        if (hour > 23) {
            *error = QStringLiteral("Invalid time value: %1").arg(describeValue(value));
            return false;
        }
        Q_UNUSED(second); // 分钟粒度丢弃秒数
        *out = QStringLiteral("%1:%2")
                   .arg(hour, 2, 10, QLatin1Char('0'))
                   .arg(minute, 2, 10, QLatin1Char('0'));
        return true;
    }
    default:
        *error = QStringLiteral("Get error type of time: %1").arg(describeValue(value));
        return false;
    }
}

// converter.py:142-148 _to_cses_time："HH:MM" → "HH:MM:00"（空值报错）
bool toCsesTime(const QString &timeStr, QString *out, QString *error)
{
    if (timeStr.isEmpty()) {
        *error = QStringLiteral("Get error type of time; value: (empty)");
        return false;
    }
    *out = timeStr + QStringLiteral(":00");
    return true;
}

// 可空字符串字段（pydantic Optional[str]）：字符串原样、缺失/null → null，
// 其它类型 → 失败（上游 pydantic 校验不通过）
bool nullableString(const QJsonValue &value, QJsonValue *out, const QString &fieldName,
                    QString *error)
{
    if (value.isString()) {
        *out = QJsonValue(value.toString());
        return true;
    }
    if (value.isNull() || value.isUndefined()) {
        *out = QJsonValue(QJsonValue::Null);
        return true;
    }
    *error = QStringLiteral("Invalid type for CSES field %1: %2")
                 .arg(fieldName, describeValue(value));
    return false;
}

// 文件读取工具
bool readTextFile(const QString &path, QString *content, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        *error = QStringLiteral("Cannot open file: %1 (%2)").arg(path, file.errorString());
        return false;
    }
    QByteArray raw = file.readAll();
    if (raw.startsWith("\xEF\xBB\xBF"))
        raw.remove(0, 3); // 上游 utf-8 打开会保留 BOM 并导致解析失败，这里宽容剥除
    *content = QString::fromUtf8(raw);
    return true;
}

bool readJsonFile(const QString &path, QJsonObject *out, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("Cannot open file: %1 (%2)").arg(path, file.errorString());
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        *error = QStringLiteral("Failed to parse JSON file %1: %2")
                     .arg(path, parseError.errorString());
        return false;
    }
    *out = doc.object();
    return true;
}

bool writeTextFile(const QString &path, const QByteArray &content, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        *error = QStringLiteral("Cannot write file: %1 (%2)").arg(path, file.errorString());
        return false;
    }
    file.write(content);
    return true;
}

// ── CW1 相关（converter.py:169-218）─────────────────────────────────────

// converter.py:169-173 _parse_cw1_part_start：part = [时, 分] → 当日分钟数
bool parseCw1PartStart(const QJsonValue &partUnit, int *minutes, QString *error)
{
    if (!partUnit.isArray() || partUnit.toArray().size() < 2) {
        *error = QStringLiteral("Invalid CW1 part definition: %1").arg(describeValue(partUnit));
        return false;
    }
    const QJsonArray unit = partUnit.toArray();
    int hour = 0;
    int minute = 0;
    if (!jsonToInt(unit.at(0), &hour) || !jsonToInt(unit.at(1), &minute)) {
        *error = QStringLiteral("Invalid CW1 part definition: %1").arg(describeValue(partUnit));
        return false;
    }
    *minutes = hour * 60 + minute;
    return true;
}

// converter.py:176-190 _sort_cw1_legacy_timeline_key 的排序键。
// "a12"/"f3" 形态的旧版键解析为 (段号, 节序号, 上课/休息)；无法解析时上游
// 会以字符串参与比较（与元组比较会抛 TypeError，即整个转换失败），这里
// 保守降级为字符串比较后再排序。
struct Cw1LegacySortKey
{
    bool structured = false;
    int part = 0;
    int classIndex = 0;
    int flag = 0; // 前缀 'a'（上课）为 0，排在其它（休息）之前
    QString raw;
};

Cw1LegacySortKey cw1LegacySortKey(const QString &name)
{
    Cw1LegacySortKey key;
    key.raw = name;
    if (name.size() > 1) {
        bool partOk = false;
        const int part = QString(name.mid(1, 1)).toInt(&partOk); // item_name[1]：单个字符
        if (partOk) {
            int classIndex = 0;
            bool classOk = true;
            if (name.size() > 2)
                classIndex = QString(name.mid(2)).toInt(&classOk); // item_name[2:]
            if (classOk) {
                key.structured = true;
                key.part = part;
                key.classIndex = classIndex;
                key.flag = name.at(0) == QLatin1Char('a') ? 0 : 1;
            }
        }
    }
    return key;
}

// converter.py:192-218 _normalize_cw1_timeline_map：
//   旧版 dict 值（键名编码类型/段/节，值为分钟时长）→ 按键排序后的
//   [type, part, classIndex, minutes] 列表；新版 list 值原样归一化为四元组。
bool normalizeCw1TimelineMap(const QJsonValue &value, QJsonValue *out, QString *error)
{
    if (value.isUndefined()) {
        *out = QJsonObject(); // cw1.get("timeline", {})：键缺失按空映射
        return true;
    }
    if (!value.isObject()) {
        // 上游 None/标量取 .items() 抛 AttributeError → 转换失败
        *error = QStringLiteral("invalid CW1 timeline format: %1").arg(describeValue(value));
        return false;
    }

    QJsonObject result;
    const QJsonObject map = value.toObject();
    for (auto it = map.begin(); it != map.end(); ++it) {
        const QString key = it.key();
        const QJsonValue &entryValue = it.value();

        if (entryValue.isObject()) {
            // 旧版格式：先按 _sort_cw1_legacy_timeline_key 排序再展开
            QList<std::pair<Cw1LegacySortKey, QJsonValue>> items;
            const QJsonObject legacy = entryValue.toObject();
            items.reserve(legacy.size());
            for (auto legacyIt = legacy.begin(); legacyIt != legacy.end(); ++legacyIt)
                items.append({ cw1LegacySortKey(legacyIt.key()), legacyIt.value() });
            std::stable_sort(items.begin(), items.end(),
                             [](const auto &a, const auto &b) {
                                 const Cw1LegacySortKey &ka = a.first;
                                 const Cw1LegacySortKey &kb = b.first;
                                 if (ka.structured && kb.structured) {
                                     if (ka.part != kb.part)
                                         return ka.part < kb.part;
                                     if (ka.classIndex != kb.classIndex)
                                         return ka.classIndex < kb.classIndex;
                                     return ka.flag < kb.flag;
                                 }
                                 return ka.raw < kb.raw;
                             });

            QJsonArray units;
            for (const auto &item : items) {
                const QString name = item.first.raw;
                if (name.isEmpty() || name.size() < 2) {
                    // 上游 item_name[0]/item_name[1] 越界抛 IndexError → 失败
                    *error = QStringLiteral("invalid CW1 timeline format: %1: %2")
                                 .arg(key, describeValue(entryValue));
                    return false;
                }
                int minutes = 0;
                if (!jsonToInt(item.second, &minutes)) {
                    *error = QStringLiteral("invalid CW1 timeline format: %1: %2")
                                 .arg(key, describeValue(entryValue));
                    return false;
                }
                int classIndex = 0;
                if (name.size() > 2 && !stringToInt(name.mid(2), &classIndex)) {
                    // int(item_name[2:]) 失败 → 上游 ValueError → 失败
                    *error = QStringLiteral("invalid CW1 timeline format: %1: %2")
                                 .arg(key, describeValue(entryValue));
                    return false;
                }
                // (1 if item_name[0] == "f" else 0, str(item_name[1]), classIndex, minutes)
                units.append(QJsonArray {
                    name.at(0) == QLatin1Char('f') ? 1 : 0,
                    QJsonValue(name.mid(1, 1)),
                    classIndex,
                    minutes,
                });
            }
            result.insert(key, units);
        } else if (entryValue.isArray()) {
            // 新版格式：[type, part, classIndex, minutes] 四元组列表
            QJsonArray units;
            const QJsonArray list = entryValue.toArray();
            for (const QJsonValue &unitValue : list) {
                if (!unitValue.isArray() || unitValue.toArray().size() != 4) {
                    // 上游 int(unit[...]) 越界抛 IndexError → 失败
                    *error = QStringLiteral("Invalid CW1 timeline unit: %1")
                                 .arg(describeValue(unitValue));
                    return false;
                }
                const QJsonArray unit = unitValue.toArray();
                int type = 0;
                int classIndex = 0;
                int minutes = 0;
                QString part;
                if (!jsonToInt(unit.at(0), &type) || !cw1NameString(unit.at(1), &part)
                    || !jsonToInt(unit.at(2), &classIndex) || !jsonToInt(unit.at(3), &minutes)) {
                    *error = QStringLiteral("Invalid CW1 timeline unit: %1")
                                 .arg(describeValue(unitValue));
                    return false;
                }
                units.append(QJsonArray { type, QJsonValue(part), classIndex, minutes });
            }
            result.insert(key, units);
        } else {
            *error = QStringLiteral("invalid CW1 timeline format: %1: %2")
                         .arg(key, describeValue(entryValue)); // converter.py:217
            return false;
        }
    }
    *out = result;
    return true;
}

// converter.py:220-249 _build_cw1_subjects：从 schedule / schedule_even 收集
// 有意义的课名，保持首次出现顺序并生成学科 id 映射
bool buildCw1Subjects(const QJsonObject &cw1, QJsonArray *subjects,
                      QHash<QString, QString> *subjectIdMap, QString *error)
{
    QStringList subjectNames;
    const QList<QString> mapNames = { QStringLiteral("schedule"), QStringLiteral("schedule_even") };
    for (const QString &mapName : mapNames) {
        const QJsonObject scheduleMap = cw1.value(mapName).toObject();
        for (auto it = scheduleMap.begin(); it != scheduleMap.end(); ++it) {
            if (!it.value().isArray())
                continue; // converter.py:227-228：非列表的课表值跳过
            const QJsonArray classes = it.value().toArray();
            for (const QJsonValue &nameValue : classes) {
                if (!isMeaningfulCw1SubjectName(nameValue))
                    continue;
                QString name;
                if (!cw1NameString(nameValue, &name)) {
                    *error = QStringLiteral("Invalid CW1 subject name: %1")
                                 .arg(describeValue(nameValue));
                    return false;
                }
                if (subjectIdMap->contains(name))
                    continue;
                subjectIdMap->insert(name, QString()); // 先占位作 seen 集合，稍后填 id
                subjectNames.append(name);
            }
        }
    }

    for (const QString &name : subjectNames) {
        const QString subjectId = ScheduleModel::generateId(QStringLiteral("subj"));
        subjectIdMap->insert(name, subjectId);
        QJsonObject subject;
        subject.insert(QStringLiteral("id"), subjectId);
        subject.insert(QStringLiteral("icon"), QStringLiteral("ic_fluent_book_20_regular"));
        subject.insert(QStringLiteral("name"), name);
        subjects->append(subject);
    }
    return true;
}

// converter.py:258-297 _build_cw1_entries：按时间线四元组展开条目，逐段累计
// 分钟数并按序填入课名
bool buildCw1Entries(const QJsonArray &timelineUnits, const QJsonArray &scheduleNames,
                     const QJsonObject &partMap, QJsonArray *entries, QString *error)
{
    int subjectIndex = 0;
    QMap<QString, int> currentMinutesByPart;
    for (const QJsonValue &unitValue : timelineUnits) {
        if (!unitValue.isArray() || unitValue.toArray().size() != 4) {
            *error = QStringLiteral("Invalid CW1 timeline unit: %1").arg(describeValue(unitValue));
            return false; // converter.py:264-265
        }
        const QJsonArray unit = unitValue.toArray();
        int unitType = 0;
        int duration = 0;
        QString partKey;
        if (!jsonToInt(unit.at(0), &unitType) || !cw1NameString(unit.at(1), &partKey)
            || !jsonToInt(unit.at(3), &duration)) {
            *error = QStringLiteral("Invalid CW1 timeline unit: %1").arg(describeValue(unitValue));
            return false;
        }

        if (!currentMinutesByPart.contains(partKey)) {
            if (!partMap.contains(partKey)) {
                *error = QStringLiteral("Unknown CW1 part key in timeline: %1").arg(partKey);
                return false; // converter.py:272-273
            }
            int startMinutes = 0;
            if (!parseCw1PartStart(partMap.value(partKey), &startMinutes, error))
                return false;
            currentMinutesByPart.insert(partKey, startMinutes);
        }

        const int startMinutes = currentMinutesByPart.value(partKey);
        const int endMinutes = startMinutes + duration;
        currentMinutesByPart[partKey] = endMinutes;

        const bool isClass = unitType == 0; // converter.py:280：0 → CLASS，否则 BREAK
        QJsonObject entry;
        entry.insert(QStringLiteral("id"), ScheduleModel::generateId(QStringLiteral("entry")));
        entry.insert(QStringLiteral("type"),
                     QJsonValue(QString::fromLatin1(
                         isClass ? ScheduleModel::kTypeClass : ScheduleModel::kTypeBreak)));
        entry.insert(QStringLiteral("startTime"), minutesToHhmm(startMinutes));
        entry.insert(QStringLiteral("endTime"), minutesToHhmm(endMinutes));

        if (isClass) {
            if (subjectIndex < scheduleNames.size()) {
                const QJsonValue nameValue = scheduleNames.at(subjectIndex);
                QString name;
                if (isMeaningfulCw1SubjectName(nameValue) && cw1NameString(nameValue, &name))
                    entry.insert(QStringLiteral("title"), name); // converter.py:292
            }
            ++subjectIndex; // converter.py:293：无论是否取到课名都自增
        }
        entries->append(entry);
    }
    return true;
}

// converter.py:299-339 _append_cw1_timeline_days：把某一套时间线/课表
// （全周或偶数周）展开为最多 7 天
bool appendCw1TimelineDays(QJsonArray *days, const QJsonObject &cw1,
                           const QHash<QString, QString> &subjectIdMap,
                           const QString &timelineKey, const QString &scheduleKey,
                           const QJsonValue &weeks, QString *error)
{
    const QJsonObject partMap = cw1.value(QStringLiteral("part")).toObject();
    const QJsonObject timelineMap = cw1.value(timelineKey).toObject();
    const QJsonObject scheduleMap = cw1.value(scheduleKey).toObject();
    const QJsonArray defaultTimeline = timelineMap.value(QStringLiteral("default")).toArray();

    for (int cw1Day = 0; cw1Day < 7; ++cw1Day) {
        const QString dayKey = QString::number(cw1Day);
        // converter.py:315：当日无时间线（或为空）时回退 default
        QJsonArray timelineUnits = defaultTimeline;
        const QJsonValue dayUnits = timelineMap.value(dayKey);
        if (dayUnits.isArray() && !dayUnits.toArray().isEmpty())
            timelineUnits = dayUnits.toArray();

        // converter.py:316 schedule_map.get(day_key, [])；非列表值上游迭代时
        // 抛 TypeError → 失败
        QJsonArray scheduleNames;
        const QJsonValue namesValue = scheduleMap.value(dayKey);
        if (namesValue.isArray()) {
            scheduleNames = namesValue.toArray();
        } else if (!namesValue.isUndefined() && !namesValue.isNull()) {
            *error = QStringLiteral("CW1 schedule for day %1 is not a list: %2")
                         .arg(dayKey, describeValue(namesValue));
            return false;
        }

        bool anyMeaningful = false; // converter.py:318-319
        for (const QJsonValue &nameValue : scheduleNames) {
            if (isMeaningfulCw1SubjectName(nameValue)) {
                anyMeaningful = true;
                break;
            }
        }
        if (!anyMeaningful)
            continue;

        QJsonArray entries;
        if (!buildCw1Entries(timelineUnits, scheduleNames, partMap, &entries, error))
            return false;

        bool anyTitledClass = false; // converter.py:322-323
        for (const QJsonValue &entryValue : entries) {
            const QJsonObject entry = entryValue.toObject();
            if (ScheduleModel::entryType(entry) == QLatin1String(ScheduleModel::kTypeClass)
                && !ScheduleModel::entryTitle(entry).isEmpty()) {
                anyTitledClass = true;
                break;
            }
        }
        if (!anyTitledClass)
            continue;

        for (int i = 0; i < entries.size(); ++i) { // converter.py:325-330
            QJsonObject entry = entries.at(i).toObject();
            if (ScheduleModel::entryType(entry) != QLatin1String(ScheduleModel::kTypeClass))
                continue;
            const QString title = ScheduleModel::entryTitle(entry);
            if (title.isEmpty())
                continue;
            const QString subjectId = subjectIdMap.value(title);
            if (!subjectId.isEmpty()) {
                entry.insert(QStringLiteral("subjectId"), subjectId);
                entry.insert(QStringLiteral("title"), QJsonValue(QJsonValue::Null)); // title = None
            }
            entries[i] = entry;
        }

        QJsonObject day;
        day.insert(QStringLiteral("id"), ScheduleModel::generateId(QStringLiteral("day")));
        day.insert(QStringLiteral("entries"), entries);
        day.insert(QStringLiteral("dayOfWeek"), QJsonArray { cw1Day + 1 }); // converter.py:336
        day.insert(QStringLiteral("weeks"), weeks);
        days->append(day);
    }
    return true;
}

// 构造 meta（converter.py:348-353 / 384-389：id/version/maxWeekCycle=2/今天）
QJsonObject makeConvertedMeta()
{
    QJsonObject meta;
    meta.insert(QStringLiteral("id"), ScheduleModel::generateId(QStringLiteral("meta")));
    meta.insert(QStringLiteral("version"), ScheduleModel::kSchemaVersion);
    meta.insert(QStringLiteral("maxWeekCycle"), 2);
    meta.insert(QStringLiteral("startDate"),
                QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"))); // str(date.today())
    return meta;
}

// ── weeks → CSES 字符串 ─────────────────────────────────────────────────

// converter.py:41-52 _convert_weeks_to_cses（day.weeks：null/字符串/整数/列表）
bool weeksToCsesKey(const QJsonValue &weeks, QString *out)
{
    switch (weeks.type()) {
    case QJsonValue::Double: {
        const double d = weeks.toDouble();
        if (d != std::floor(d)) {
            *out = QStringLiteral("all"); // 非整数不是 int，走 else 分支
            return true;
        }
        *out = pythonMod(static_cast<int>(d), 2) == 1 ? QStringLiteral("odd")
                                                     : QStringLiteral("even");
        return true;
    }
    case QJsonValue::Bool: // Python bool 是 int 子类：True → 1 → "odd"
        *out = weeks.toBool() ? QStringLiteral("odd") : QStringLiteral("even");
        return true;
    default: // null / 字符串（WeekType 只可能是 "all"）/ 列表 → "all"
        *out = QStringLiteral("all");
        return true;
    }
}

// converter.py:453-466 _ov_weeks_to_keys（override.weeks → 周次键列表）
bool weeksToCsesKeys(const QJsonValue &weeks, QStringList *out, QString *error)
{
    switch (weeks.type()) {
    case QJsonValue::Double: {
        const double d = weeks.toDouble();
        if (d != std::floor(d)) {
            *out = { QStringLiteral("all") };
            return true;
        }
        *out = { pythonMod(static_cast<int>(d), 2) == 1 ? QStringLiteral("odd")
                                                        : QStringLiteral("even") };
        return true;
    }
    case QJsonValue::Bool:
        *out = { weeks.toBool() ? QStringLiteral("odd") : QStringLiteral("even") };
        return true;
    case QJsonValue::Array: {
        bool allOdd = true;
        bool allEven = true;
        const QJsonArray list = weeks.toArray();
        for (const QJsonValue &w : list) {
            int weekNumber = 0;
            if (!jsonToInt(w, &weekNumber)) {
                // 上游 w % 2 对非数值抛 TypeError → 导出失败
                *error = QStringLiteral("Invalid week number in override weeks: %1")
                             .arg(describeValue(w));
                return false;
            }
            if (pythonMod(weekNumber, 2) == 1)
                allEven = false;
            else
                allOdd = false;
        }
        // 空列表时 allOdd/allEven 均为真 → 走第一个判断（与 all() 空为真一致）
        if (allOdd)
            *out = { QStringLiteral("odd") };
        else if (allEven)
            *out = { QStringLiteral("even") };
        else
            *out = { QStringLiteral("all") };
        return true;
    }
    default: // null（None）与字符串（WeekType）
        *out = { QStringLiteral("all") };
        return true;
    }
}

// ── CSES YAML 子集解析 ──────────────────────────────────────────────────

struct YLine
{
    int indent = 0;
    QString content;
};

// 预处理：拆行、统计缩进、去空行/整行注释/文档分隔符；Tab 缩进直接报错
bool splitYamlLines(const QString &text, QList<YLine> *lines, QString *error)
{
    const QStringList rawLines = text.split(QLatin1Char('\n'));
    for (const QString &rawLine : rawLines) {
        int indent = 0;
        while (indent < rawLine.size() && rawLine.at(indent) == QLatin1Char(' '))
            ++indent;
        if (indent < rawLine.size() && rawLine.at(indent) == QLatin1Char('\t')) {
            *error = QStringLiteral("YAML tab indentation is not supported");
            return false;
        }
        QString content = rawLine.mid(indent);
        while (!content.isEmpty()
               && (content.endsWith(QLatin1Char(' ')) || content.endsWith(QLatin1Char('\r'))))
            content.chop(1);
        if (content.isEmpty() || content.startsWith(QLatin1Char('#')))
            continue;
        if (content == QLatin1String("---"))
            continue; // 文档起始分隔符（仅支持单文档）
        if (content == QLatin1String("..."))
            break;    // 文档结束分隔符
        lines->append({ indent, content });
    }
    return true;
}

// 找到映射键分隔冒号：冒号后必须紧跟空格或行尾，跳过引号包裹的键
int findKeyColon(const QString &text)
{
    bool inSingle = false;
    bool inDouble = false;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (inSingle) {
            if (c == QLatin1Char('\''))
                inSingle = false;
        } else if (inDouble) {
            if (c == QLatin1Char('\\'))
                ++i;
            else if (c == QLatin1Char('"'))
                inDouble = false;
        } else if (c == QLatin1Char('\'')) {
            inSingle = true;
        } else if (c == QLatin1Char('"')) {
            inDouble = true;
        } else if (c == QLatin1Char(':')) {
            if (i + 1 >= text.size() || text.at(i + 1) == QLatin1Char(' '))
                return i;
        }
    }
    return -1;
}

// 去掉键两侧引号（键一般是裸标识符，引号键仅作兼容）
QString unquoteKey(const QString &key)
{
    const QString trimmed = key.trimmed();
    if (trimmed.size() >= 2 && trimmed.startsWith(QLatin1Char('"'))
        && trimmed.endsWith(QLatin1Char('"')))
        return trimmed.mid(1, trimmed.size() - 2);
    if (trimmed.size() >= 2 && trimmed.startsWith(QLatin1Char('\''))
        && trimmed.endsWith(QLatin1Char('\'')))
        return trimmed.mid(1, trimmed.size() - 2);
    return trimmed;
}

bool parseYamlScalar(QString text, QJsonValue *out, QString *error);

// 双引号标量：YAML 转义子集（JSON 兼容 + \x/\u/\N/\_/\L/\P）
bool parseDoubleQuoted(const QString &text, QString *out, QString *error)
{
    QString result;
    int i = 1;
    while (i < text.size()) {
        const QChar c = text.at(i);
        if (c == QLatin1Char('"'))
            break;
        if (c == QLatin1Char('\\')) {
            ++i;
            if (i >= text.size()) {
                *error = QStringLiteral("Unterminated double-quoted scalar");
                return false;
            }
            const QChar e = text.at(i);
            switch (e.unicode()) {
            case '0': result.append(QChar(0)); break;
            case 'a': result.append(QChar(0x07)); break;
            case 'b': result.append(QChar(0x08)); break;
            case 't': result.append(QLatin1Char('\t')); break;
            case 'n': result.append(QLatin1Char('\n')); break;
            case 'v': result.append(QChar(0x0B)); break;
            case 'f': result.append(QChar(0x0C)); break;
            case 'r': result.append(QLatin1Char('\r')); break;
            case 'e': result.append(QChar(0x1B)); break;
            case ' ': result.append(QLatin1Char(' ')); break;
            case '"': result.append(QLatin1Char('"')); break;
            case '/': result.append(QLatin1Char('/')); break;
            case '\\': result.append(QLatin1Char('\\')); break;
            case 'N': result.append(QChar(0x85)); break;
            case '_': result.append(QChar(0xA0)); break;
            case 'L': result.append(QChar(0x2028)); break;
            case 'P': result.append(QChar(0x2029)); break;
            case 'x':
            case 'u': {
                const int width = e == QLatin1Char('x') ? 2 : 4;
                if (i + width >= text.size()) {
                    *error = QStringLiteral("Invalid escape sequence in scalar");
                    return false;
                }
                bool ok = false;
                const uint code = text.mid(i + 1, width).toUInt(&ok, 16);
                if (!ok) {
                    *error = QStringLiteral("Invalid escape sequence in scalar");
                    return false;
                }
                result.append(QChar(static_cast<char16_t>(code)));
                i += width;
                break;
            }
            default:
                *error = QStringLiteral("Unsupported escape sequence '\\%1' in scalar").arg(e);
                return false;
            }
        } else {
            result.append(c);
        }
        ++i;
    }
    if (i >= text.size()) {
        *error = QStringLiteral("Unterminated double-quoted scalar");
        return false;
    }
    *out = result;
    return true;
}

// 单引号标量：'' 为转义的单引号
bool parseSingleQuoted(const QString &text, QString *out, QString *error)
{
    QString result;
    int i = 1;
    while (i < text.size()) {
        const QChar c = text.at(i);
        if (c == QLatin1Char('\'')) {
            if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('\'')) {
                result.append(QLatin1Char('\''));
                ++i;
            } else {
                break;
            }
        } else {
            result.append(c);
        }
        ++i;
    }
    if (i >= text.size()) {
        *error = QStringLiteral("Unterminated single-quoted scalar");
        return false;
    }
    *out = result;
    return true;
}

// 标量解析：引号 → 流式 [] {} / JSON → plain（null/bool/int/字符串）。
// 不支持八进制/十六进制/性别数/浮点/时间戳等隐式标量（超出 CSES 所需子集）。
bool parseYamlScalar(QString text, QJsonValue *out, QString *error)
{
    if (text.startsWith(QLatin1String("'"))) {
        QString value;
        if (!parseSingleQuoted(text, &value, error))
            return false;
        *out = value;
        return true;
    }
    if (text.startsWith(QLatin1String("\""))) {
        QString value;
        if (!parseDoubleQuoted(text, &value, error))
            return false;
        *out = value;
        return true;
    }
    if (text == QLatin1String("[]")) {
        *out = QJsonArray();
        return true;
    }
    if (text == QLatin1String("{}")) {
        *out = QJsonObject();
        return true;
    }
    if (text.startsWith(QLatin1Char('[')) || text.startsWith(QLatin1Char('{'))) {
        // 流式集合：直接交给 JSON 解析器（JSON 是 YAML 流式风格的子集）
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError
            || (!doc.isArray() && !doc.isObject())) {
            *error = QStringLiteral("Failed to parse flow collection: %1 (%2)")
                         .arg(text, parseError.errorString());
            return false;
        }
        *out = doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object());
        return true;
    }

    // plain：去掉 " #" 行内注释
    const int comment = text.indexOf(QLatin1String(" #"));
    if (comment >= 0)
        text = text.left(comment);
    text = text.trimmed();
    if (text.isEmpty()) {
        *out = QJsonValue(QJsonValue::Null);
        return true;
    }
    // 空值
    if (text == QLatin1String("~") || text == QLatin1String("null")
        || text == QLatin1String("Null") || text == QLatin1String("NULL")) {
        *out = QJsonValue(QJsonValue::Null);
        return true;
    }
    // 布尔（PyYAML 1.1 resolver 的字面集合）
    static const char *kBools[] = { "yes",   "Yes",   "YES",  "no",    "No",   "NO",
                                    "true",  "True",  "TRUE", "false", "False", "FALSE",
                                    "on",    "On",    "ON",   "off",   "Off",  "OFF" };
    for (const char *candidate : kBools) {
        if (text == QLatin1String(candidate)) {
            const QString lower = text.toLower();
            const bool value = lower == QLatin1String("yes") || lower == QLatin1String("true")
                               || lower == QLatin1String("on");
            *out = value;
            return true;
        }
    }
    // 十进制整数（可选正负号）
    {
        int pos = 0;
        bool negative = false;
        if (text.at(0) == QLatin1Char('+') || text.at(0) == QLatin1Char('-')) {
            negative = text.at(0) == QLatin1Char('-');
            pos = 1;
        }
        if (pos < text.size() && text.size() - pos <= 9) {
            bool allDigits = true;
            for (int i = pos; i < text.size(); ++i) {
                if (!text.at(i).isDigit()) {
                    allDigits = false;
                    break;
                }
            }
            if (allDigits) {
                *out = text.mid(pos).toInt() * (negative ? -1 : 1);
                return true;
            }
        }
    }
    *out = text; // 其余一律按字符串（含 "08:45:00" 这类带冒号的值）
    return true;
}

// 解析 YAML 块级节点；*i 为当前行下标，返回后指向下一个未消费的行
bool parseYamlNode(QList<YLine> *lines, int *i, int depth, QJsonValue *out, QString *error);

bool parseYamlMapping(QList<YLine> *lines, int *i, int depth, QJsonValue *out, QString *error)
{
    if (depth > 200) {
        *error = QStringLiteral("YAML nesting too deep");
        return false;
    }
    QJsonObject obj;
    const int indent = lines->at(*i).indent;
    while (*i < lines->size()) {
        const YLine line = lines->at(*i);
        if (line.indent > indent) {
            *error = QStringLiteral("Unexpected YAML indentation: %1").arg(line.content);
            return false;
        }
        if (line.indent < indent)
            break; // 缩进回退 → 本映射结束
        if (line.content == QLatin1String("-") || line.content.startsWith(QLatin1String("- ")))
            break; // 同缩进序列 → 属于上一个键（PyYAML 风格）或上层

        const int colon = findKeyColon(line.content);
        if (colon < 0) {
            *error = QStringLiteral("Cannot parse YAML mapping key: %1").arg(line.content);
            return false;
        }
        const QString key = unquoteKey(line.content.left(colon));
        const QString rest = line.content.mid(colon + 1).trimmed();

        if (rest.isEmpty()) {
            ++(*i);
            // 值在后续行：更深的节点，或与键同缩进的序列
            if (*i < lines->size()) {
                if (lines->at(*i).indent > indent) {
                    QJsonValue value;
                    if (!parseYamlNode(lines, i, depth + 1, &value, error))
                        return false;
                    obj.insert(key, value);
                    continue;
                }
                const QString &next = lines->at(*i).content;
                if (lines->at(*i).indent == indent
                    && (next == QLatin1String("-") || next.startsWith(QLatin1String("- ")))) {
                    QJsonValue value;
                    if (!parseYamlNode(lines, i, depth + 1, &value, error))
                        return false;
                    obj.insert(key, value);
                    continue;
                }
            }
            obj.insert(key, QJsonValue(QJsonValue::Null)); // "key:" 且无后续内容 → null
        } else {
            QJsonValue value;
            if (!parseYamlScalar(rest, &value, error))
                return false;
            obj.insert(key, value);
            ++(*i);
        }
    }
    *out = obj;
    return true;
}

bool parseYamlSequence(QList<YLine> *lines, int *i, int depth, QJsonValue *out, QString *error)
{
    if (depth > 200) {
        *error = QStringLiteral("YAML nesting too deep");
        return false;
    }
    QJsonArray arr;
    const int indent = lines->at(*i).indent;
    while (*i < lines->size()) {
        const YLine line = lines->at(*i);
        if (line.indent > indent) {
            *error = QStringLiteral("Unexpected YAML indentation: %1").arg(line.content);
            return false;
        }
        if (line.indent < indent)
            break;
        const bool isItem =
            line.content == QLatin1String("-") || line.content.startsWith(QLatin1String("- "));
        if (!isItem)
            break;

        // 项内容：'-' 之后跳过空格，内容列号作为内联节点的虚拟缩进
        int pos = 1;
        while (pos < line.content.size() && line.content.at(pos) == QLatin1Char(' '))
            ++pos;
        if (pos >= line.content.size()) {
            ++(*i); // 纯 "-"：值在后续更深的行
            if (*i < lines->size() && lines->at(*i).indent > indent) {
                QJsonValue value;
                if (!parseYamlNode(lines, i, depth + 1, &value, error))
                    return false;
                arr.append(value);
            } else {
                arr.append(QJsonValue(QJsonValue::Null)); // "- " 空项 → null
            }
        } else {
            const QString rest = line.content.mid(pos);
            const int virtualIndent = indent + pos;
            if (rest == QLatin1String("-") || rest.startsWith(QLatin1String("- "))
                || findKeyColon(rest) >= 0) {
                // 内联映射/嵌套序列：把当前行改写为虚拟行后按正常节点解析
                (*lines)[*i] = { virtualIndent, rest };
                QJsonValue value;
                if (!parseYamlNode(lines, i, depth + 1, &value, error))
                    return false;
                arr.append(value);
            } else {
                QJsonValue value;
                if (!parseYamlScalar(rest, &value, error))
                    return false;
                arr.append(value);
                ++(*i);
            }
        }
    }
    *out = arr;
    return true;
}

bool parseYamlNode(QList<YLine> *lines, int *i, int depth, QJsonValue *out, QString *error)
{
    if (*i >= lines->size()) {
        *out = QJsonValue(QJsonValue::Null);
        return true;
    }
    const YLine &line = lines->at(*i);
    const bool isSequenceItem = line.content == QLatin1String("-")
                                || line.content.startsWith(QLatin1String("- "));
    if (isSequenceItem)
        return parseYamlSequence(lines, i, depth, out, error);
    return parseYamlMapping(lines, i, depth, out, error);
}

// ── CSES YAML 发射（PyYAML safe_dump 风格）──────────────────────────────

// plain 标量判定：首字符必须是字母或下划线（天然排除数字/日期/指示符开头的
// 歧义形态），字符集限定为字母/数字/空格/-_./，且不命中布尔与空值保留字。
// 不满足时一律加单引号（多余但无害，语义与 PyYAML 输出等价）。
bool isPlainSafeScalar(const QString &s)
{
    if (s.isEmpty())
        return false;
    const QChar first = s.at(0);
    if (!first.isLetter() && first != QLatin1Char('_'))
        return false;
    if (s != s.trimmed())
        return false;
    static const char *kReserved[] = { "true", "false", "yes", "no",  "on",
                                       "off",  "y",     "n",   "null", "none" };
    const QString lower = s.toLower();
    for (const char *word : kReserved) {
        if (lower == QLatin1String(word))
            return false;
    }
    for (const QChar &c : s) {
        if (c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('-')
            || c == QLatin1Char('.') || c == QLatin1Char('/') || c == QLatin1Char(' '))
            continue;
        return false;
    }
    return true;
}

QString scalarText(const QJsonValue &value)
{
    switch (value.type()) {
    case QJsonValue::Bool:
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QJsonValue::Double: {
        const double d = value.toDouble();
        if (d == static_cast<double>(static_cast<qint64>(d)))
            return QString::number(static_cast<qint64>(d));
        return QString::number(d, 'g', 15);
    }
    case QJsonValue::String: {
        const QString s = value.toString();
        const bool hasControl = std::any_of(s.cbegin(), s.cend(), [](const QChar &c) {
            return c.unicode() < 0x20 || c == QLatin1Char('\t');
        });
        if (hasControl) {
            // 控制字符走双引号风格（CSES 数据不会出现，仅兜底）
            QString escaped;
            for (const QChar &c : s) {
                switch (c.unicode()) {
                case '"': escaped.append(QLatin1String("\\\"")); break;
                case '\\': escaped.append(QLatin1String("\\\\")); break;
                case '\n': escaped.append(QLatin1String("\\n")); break;
                case '\r': escaped.append(QLatin1String("\\r")); break;
                case '\t': escaped.append(QLatin1String("\\t")); break;
                default:
                    if (c.unicode() < 0x20)
                        escaped.append(QStringLiteral("\\u%1")
                                           .arg(static_cast<int>(c.unicode()), 4, 16, QLatin1Char('0')));
                    else
                        escaped.append(c);
                }
            }
            return QLatin1Char('"') + escaped + QLatin1Char('"');
        }
        if (isPlainSafeScalar(s))
            return s;
        return QLatin1Char('\'') + QString(s).replace(QLatin1Char('\''), QLatin1String("''"))
             + QLatin1Char('\'');
    }
    default:
        return QStringLiteral("null"); // null / undefined
    }
}

QString indentSpaces(int indent)
{
    return QString(indent, QLatin1Char(' '));
}

void appendValueText(const QJsonValue &value, int indent, QString *out);

void emitYamlMapping(const QJsonObject &obj, int indent, QString *out)
{
    if (obj.isEmpty()) {
        out->append(QLatin1String("{}\n"));
        return;
    }
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        out->append(indentSpaces(indent));
        out->append(scalarText(it.key()));
        out->append(QLatin1Char(':'));
        appendValueText(it.value(), indent, out);
    }
}

void emitYamlSequence(const QJsonArray &arr, int indent, QString *out)
{
    for (const QJsonValue &item : arr) {
        out->append(indentSpaces(indent));
        out->append(QLatin1Char('-'));
        if (item.isObject()) {
            const QJsonObject obj = item.toObject();
            if (obj.isEmpty()) {
                out->append(QLatin1String(" {}\n"));
                continue;
            }
            // 首个键内联在 "-" 后，其余键对齐到 dashIndent + 2
            auto it = obj.begin();
            out->append(QLatin1String(" "));
            out->append(scalarText(it.key()));
            out->append(QLatin1Char(':'));
            appendValueText(it.value(), indent + 2, out);
            for (++it; it != obj.end(); ++it) {
                out->append(indentSpaces(indent + 2));
                out->append(scalarText(it.key()));
                out->append(QLatin1Char(':'));
                appendValueText(it.value(), indent + 2, out);
            }
        } else if (item.isArray()) {
            const QJsonArray inner = item.toArray();
            if (inner.isEmpty()) {
                out->append(QLatin1String(" []\n"));
                continue;
            }
            out->append(QLatin1Char('\n'));
            emitYamlSequence(inner, indent + 2, out);
        } else {
            out->append(QLatin1Char(' '));
            out->append(scalarText(item));
            out->append(QLatin1Char('\n'));
        }
    }
}

void appendValueText(const QJsonValue &value, int indent, QString *out)
{
    switch (value.type()) {
    case QJsonValue::Object: {
        const QJsonObject obj = value.toObject();
        if (obj.isEmpty()) {
            out->append(QLatin1String(" {}\n"));
        } else {
            out->append(QLatin1Char('\n'));
            emitYamlMapping(obj, indent + 2, out); // 嵌套映射缩进 +2
        }
        break;
    }
    case QJsonValue::Array: {
        const QJsonArray arr = value.toArray();
        if (arr.isEmpty()) {
            out->append(QLatin1String(" []\n"));
        } else {
            out->append(QLatin1Char('\n'));
            emitYamlSequence(arr, indent, out); // 序列与键同缩进（PyYAML 风格）
        }
        break;
    }
    default:
        out->append(QLatin1Char(' '));
        out->append(scalarText(value));
        out->append(QLatin1Char('\n'));
        break;
    }
}

} // namespace

// ── 公共接口 ─────────────────────────────────────────────────────────────

QString localizedDayName(int dow)
{
    // converter.py:54-57 get_localized_day_name
    return QLocale().dayName(dow, QLocale::LongFormat);
}

QString localizedWeekLabel(const QString &weekStr)
{
    // converter.py:59-68 get_localized_week_label
    if (weekStr == QLatin1String("all"))
        return QCoreApplication::translate("Schedule", "All Weeks");
    if (weekStr == QLatin1String("odd"))
        return QCoreApplication::translate("Schedule", "Odd Weeks");
    if (weekStr == QLatin1String("even"))
        return QCoreApplication::translate("Schedule", "Even Weeks");
    return weekStr;
}

bool parseCsesDocument(const QByteArray &text, QJsonObject *outCses, QString *error)
{
    const QByteArray trimmed = text.trimmed();
    if (trimmed.startsWith('{') || trimmed.startsWith('[')) {
        // 纯 JSON 文档（YAML 1.2 的流式风格）直接走 QJsonDocument
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(text, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            *error = QStringLiteral("Failed to parse CSES JSON document: %1")
                         .arg(parseError.errorString());
            return false;
        }
        *outCses = doc.object();
        return true;
    }

    QList<YLine> lines;
    if (!splitYamlLines(QString::fromUtf8(text), &lines, error))
        return false;
    if (lines.isEmpty()) {
        *error = QStringLiteral("CSES document is empty");
        return false;
    }
    int i = 0;
    QJsonValue root;
    if (!parseYamlNode(&lines, &i, 0, &root, error))
        return false;
    if (!root.isObject()) {
        // 上游根不是 dict 时取 .keys() 抛 AttributeError → 失败
        *error = QStringLiteral("CSES document root must be a mapping");
        return false;
    }
    *outCses = root.toObject();
    return true;
}

QByteArray dumpCsesYaml(const QJsonObject &cses)
{
    QString out;
    emitYamlMapping(cses, 0, &out);
    return out.toUtf8();
}

bool loadCses(const QString &path, QJsonObject *outCses, QString *error)
{
    // converter.py:71-85 from_cses
    QString text;
    if (!readTextFile(path, &text, error)) {
        cwn::Log::error(QStringLiteral("CSES file not found: %1").arg(path));
        return false;
    }
    if (!parseCsesDocument(text.toUtf8(), outCses, error)) {
        cwn::Log::error(QStringLiteral("Failed to parse CSES YAML file: %1\n%2").arg(path, *error));
        return false;
    }
    // converter.py:123-130 _validate（cses 分支）
    QStringList missing;
    for (const char *key : { "version", "subjects", "schedules" }) {
        if (!outCses->contains(QLatin1String(key)))
            missing.append(QLatin1String(key));
    }
    if (!missing.isEmpty()) {
        *error = QStringLiteral("CSES data is missing required keys: %1").arg(missing.join(", "));
        return false;
    }
    const QJsonValue version = outCses->value(QStringLiteral("version"));
    if (!version.isDouble() || version.toInt() != kCsesSchemaVersion) {
        *error = QStringLiteral("CSES schema version not supported: %1").arg(describeValue(version));
        return false;
    }
    return true;
}

bool loadCw2(const QString &path, QJsonObject *outSchedule, QString *error)
{
    // converter.py:87-101 from_cw2
    if (!readJsonFile(path, outSchedule, error)) {
        cwn::Log::error(QStringLiteral("CW2 file not found: %1").arg(path));
        return false;
    }
    // converter.py:131-135 _validate（cw2 分支）
    const QJsonValue meta = outSchedule->value(QStringLiteral("meta"));
    if (!meta.isObject() || meta.toObject().value(QStringLiteral("version")).isUndefined()) {
        *error = QStringLiteral("CW2 data missing 'meta' or 'version'");
        return false;
    }
    const QJsonValue version = meta.toObject().value(QStringLiteral("version"));
    if (!version.isDouble() || version.toInt() != ScheduleModel::kSchemaVersion) {
        *error = QStringLiteral("CW2 schema version not supported: %1").arg(describeValue(version));
        return false;
    }
    return true;
}

bool loadCw1(const QString &path, QJsonObject *outCw1, QString *error)
{
    // converter.py:103-117 from_cw1
    if (!readJsonFile(path, outCw1, error)) {
        cwn::Log::error(QStringLiteral("CW1 file not found: %1").arg(path));
        return false;
    }
    // converter.py:136-140 _validate（cw1 分支）
    QStringList missing;
    for (const char *key : { "part", "part_name", "timeline", "schedule" }) {
        if (!outCw1->contains(QLatin1String(key)))
            missing.append(QLatin1String(key));
    }
    if (!missing.isEmpty()) {
        *error = QStringLiteral("CW1 data is missing required keys: %1").arg(missing.join(", "));
        return false;
    }
    return true;
}

bool csesToCw2(const QJsonObject &cses, QJsonObject *outSchedule, QString *error)
{
    // converter.py:381-445 _convert_cses_to_cw2
    QJsonArray subjects;
    QHash<QString, QString> subjIdMap; // name → id

    const QJsonArray csesSubjects = cses.value(QStringLiteral("subjects")).toArray();
    for (const QJsonValue &subjectValue : csesSubjects) {
        if (!subjectValue.isObject()) {
            *error = QStringLiteral("Invalid CSES subject entry: %1")
                         .arg(describeValue(subjectValue));
            return false; // 上游 subj["name"] 对标量抛 TypeError
        }
        const QJsonObject subj = subjectValue.toObject();
        const QJsonValue nameValue = subj.value(QStringLiteral("name"));
        if (!nameValue.isString()) {
            // 上游 subj["name"] KeyError 或 pydantic 对 null 名称校验失败
            *error = QStringLiteral("CSES subject has no valid name: %1")
                         .arg(describeValue(subjectValue));
            return false;
        }
        const QString name = nameValue.toString();
        const QString subjectId = ScheduleModel::generateId(QStringLiteral("subj"));
        subjIdMap.insert(name, subjectId); // converter.py:395

        QJsonObject subject;
        subject.insert(QStringLiteral("id"), subjectId);
        subject.insert(QStringLiteral("icon"), QStringLiteral("ic_fluent_book_20_regular"));
        subject.insert(QStringLiteral("name"), name);
        QJsonValue simplified;
        QJsonValue teacher;
        QJsonValue location;
        if (!nullableString(subj.value(QStringLiteral("simplified_name")), &simplified,
                            QStringLiteral("simplified_name"), error)
            || !nullableString(subj.value(QStringLiteral("teacher")), &teacher,
                               QStringLiteral("teacher"), error)
            || !nullableString(subj.value(QStringLiteral("room")), &location,
                               QStringLiteral("room"), error)) {
            return false;
        }
        subject.insert(QStringLiteral("simplifiedName"), simplified); // converter.py:400
        subject.insert(QStringLiteral("teacher"), teacher);           // converter.py:401
        subject.insert(QStringLiteral("location"), location);         // converter.py:402（room）
        subjects.append(subject);
    }

    QJsonArray days;
    const QJsonArray csesSchedules = cses.value(QStringLiteral("schedules")).toArray();
    for (const QJsonValue &scheduleValue : csesSchedules) {
        if (!scheduleValue.isObject()) {
            *error = QStringLiteral("Invalid CSES schedule entry: %1")
                         .arg(describeValue(scheduleValue));
            return false;
        }
        const QJsonObject sch = scheduleValue.toObject();

        QJsonArray entries;
        const QJsonArray classes = sch.value(QStringLiteral("classes")).toArray();
        for (const QJsonValue &classValue : classes) {
            if (!classValue.isObject()) {
                *error = QStringLiteral("Invalid CSES class entry: %1")
                             .arg(describeValue(classValue));
                return false;
            }
            const QJsonObject cls = classValue.toObject();

            // converter.py:410-414：按名匹配学科，缺失时建临时学科
            QString subjectId;
            QString subjectName;
            const QJsonValue subjectValueInClass = cls.value(QStringLiteral("subject"));
            if (subjectValueInClass.isString()) {
                subjectName = subjectValueInClass.toString();
                subjectId = subjIdMap.value(subjectName);
            } else if (!subjectValueInClass.isNull() && !subjectValueInClass.isUndefined()) {
                // 上游：非字符串名先被 map.get() 判失，再 or 成学科名后
                // pydantic 校验失败 → 整体失败
                *error = QStringLiteral("Invalid CSES class subject: %1")
                             .arg(describeValue(subjectValueInClass));
                return false;
            }
            if (subjectId.isEmpty()) {
                cwn::Log::warn(QStringLiteral("No matching subject for '%1', creating temporary "
                                              "subject.")
                                   .arg(subjectName)); // converter.py:412
                subjectId = ScheduleModel::generateId(QStringLiteral("subj_temp"));
                QJsonObject temp;
                temp.insert(QStringLiteral("id"), subjectId);
                temp.insert(QStringLiteral("name"), subjectName.isEmpty()
                                                       ? QStringLiteral("Unknown Subject")
                                                       : subjectName); // converter.py:414
                subjects.append(temp);
            }

            QString startTime;
            QString endTime;
            if (!toCwTime(cls.value(QStringLiteral("start_time")), &startTime, error))
                return false; // converter.py:421
            if (!toCwTime(cls.value(QStringLiteral("end_time")), &endTime, error))
                return false; // converter.py:422

            QJsonObject entry;
            entry.insert(QStringLiteral("id"), ScheduleModel::generateId(QStringLiteral("entry")));
            entry.insert(QStringLiteral("type"),
                         QJsonValue(QString::fromLatin1(ScheduleModel::kTypeClass)));
            entry.insert(QStringLiteral("subjectId"), subjectId);
            entry.insert(QStringLiteral("startTime"), startTime);
            entry.insert(QStringLiteral("endTime"), endTime);
            entries.append(entry);
        }

        // converter.py:425-431：仅字符串 "odd"/"even" 有映射，其余（含 null/整数）→ "all"
        const QJsonValue weeksValue = sch.value(QStringLiteral("weeks"));
        QJsonValue weeks;
        if (weeksValue.isString() && weeksValue.toString() == QLatin1String("odd"))
            weeks = QJsonValue(1);
        else if (weeksValue.isString() && weeksValue.toString() == QLatin1String("even"))
            weeks = QJsonValue(2);
        else
            weeks = QJsonValue(QStringLiteral("all"));

        // converter.py:436：enable_day 真值时才有 dayOfWeek（0/null/缺失 → null）
        QJsonValue dayOfWeek = QJsonValue(QJsonValue::Null);
        const QJsonValue enableDay = sch.value(QStringLiteral("enable_day"));
        if (enableDay.isBool()) {
            if (enableDay.toBool())
                dayOfWeek = QJsonArray { 1 }; // True → [True] → pydantic → [1]
        } else if (enableDay.isString()) {
            bool ok = false;
            const int day = enableDay.toString().toInt(&ok); // pydantic 宽松模式接受 "1"
            if (!ok) {
                *error = QStringLiteral("Invalid CSES enable_day: %1").arg(enableDay.toString());
                return false;
            }
            if (day != 0)
                dayOfWeek = QJsonArray { day };
        } else if (enableDay.isDouble()) {
            if (enableDay.toDouble() != 0.0)
                dayOfWeek = QJsonArray { enableDay.toInt() };
        }

        QJsonObject day;
        day.insert(QStringLiteral("id"), ScheduleModel::generateId(QStringLiteral("day")));
        day.insert(QStringLiteral("entries"), entries);
        day.insert(QStringLiteral("dayOfWeek"), dayOfWeek);
        day.insert(QStringLiteral("weeks"), weeks);
        days.append(day);
    }

    QJsonObject schedule;
    schedule.insert(QStringLiteral("meta"), makeConvertedMeta());
    schedule.insert(QStringLiteral("subjects"), subjects);
    schedule.insert(QStringLiteral("days"), days);
    schedule.insert(QStringLiteral("overrides"), QJsonArray());
    // 补齐 model_dump 形状（缺省字段 → null 等，见 ScheduleModel.h）
    *outSchedule = ScheduleModel::normalizeSchedule(schedule);
    return true;
}

bool cw1ToCw2(const QJsonObject &cw1Raw, QJsonObject *outSchedule, QString *error)
{
    // converter.py:341-378 _convert_cw1_to_cw2
    QJsonObject cw1 = cw1Raw;
    QJsonValue normalized;
    if (!normalizeCw1TimelineMap(cw1.value(QStringLiteral("timeline")), &normalized, error))
        return false;
    cw1.insert(QStringLiteral("timeline"), normalized);
    if (!normalizeCw1TimelineMap(cw1.value(QStringLiteral("timeline_even")), &normalized, error))
        return false;
    cw1.insert(QStringLiteral("timeline_even"), normalized);

    QJsonArray subjects;
    QHash<QString, QString> subjectIdMap;
    if (!buildCw1Subjects(cw1, &subjects, &subjectIdMap, error))
        return false;

    QJsonArray days;
    if (!appendCw1TimelineDays(&days, cw1, subjectIdMap, QStringLiteral("timeline"),
                               QStringLiteral("schedule"), QJsonValue(QStringLiteral("all")),
                               error))
        return false; // converter.py:358（WeekType.ALL）

    // converter.py:360-371：偶数周时间线/课表任一存在才展开（weeks=2）
    const QJsonObject evenTimeline = cw1.value(QStringLiteral("timeline_even")).toObject();
    bool hasEvenTimeline = false;
    for (const char *key : { "default", "0", "1", "2", "3", "4", "5", "6" }) {
        const QJsonValue value = evenTimeline.value(QLatin1String(key));
        if (value.isArray() && !value.toArray().isEmpty()) {
            hasEvenTimeline = true;
            break;
        }
    }
    const QJsonObject evenSchedule = cw1.value(QStringLiteral("schedule_even")).toObject();
    bool hasEvenSchedule = false;
    for (int day = 0; day < 7 && !hasEvenSchedule; ++day) {
        const QJsonValue value = evenSchedule.value(QString::number(day));
        if (!value.isArray())
            continue;
        for (const QJsonValue &nameValue : value.toArray()) {
            if (isMeaningfulCw1SubjectName(nameValue)) {
                hasEvenSchedule = true;
                break;
            }
        }
    }
    if (hasEvenTimeline || hasEvenSchedule) {
        if (!appendCw1TimelineDays(&days, cw1, subjectIdMap, QStringLiteral("timeline_even"),
                                   QStringLiteral("schedule_even"), QJsonValue(2), error))
            return false;
    }

    QJsonObject schedule;
    schedule.insert(QStringLiteral("meta"), makeConvertedMeta());
    schedule.insert(QStringLiteral("subjects"), subjects);
    schedule.insert(QStringLiteral("days"), days);
    schedule.insert(QStringLiteral("overrides"), QJsonArray());
    *outSchedule = ScheduleModel::normalizeSchedule(schedule);
    return true;
}

bool cw2ToCses(const QJsonObject &schedule, QJsonObject *outCses, QString *error)
{
    // converter.py:448-560 _convert_cw2_to_cses
    const QJsonArray subjectArray = ScheduleModel::subjects(schedule);
    QHash<QString, QJsonObject> subjectsMap; // id → subject
    for (const QJsonValue &subjectValue : subjectArray) {
        const QJsonObject subject = subjectValue.toObject();
        subjectsMap.insert(subject.value(QStringLiteral("id")).toString(), subject);
    }

    // override_map[(dow, week_key)] → override 列表（converter.py:468-474）
    QMap<std::pair<int, QString>, QJsonArray> overrideMap;
    for (const QJsonValue &overrideValue : ScheduleModel::overrides(schedule)) {
        const QJsonObject override_ = overrideValue.toObject();
        // o.dayOfWeek or [0]：null/缺失/空列表 → [0]（converter.py:470）
        QList<int> dows;
        const QStringList dowStrings = ScheduleModel::dayOfWeekList(override_.value(QStringLiteral("dayOfWeek")));
        if (dowStrings.isEmpty()) {
            dows.append(0);
        } else {
            for (const QString &dowString : dowStrings)
                dows.append(dowString.toInt());
        }
        QStringList keys;
        if (!weeksToCsesKeys(override_.value(QStringLiteral("weeks")), &keys, error))
            return false;
        for (const int dow : dows) {
            for (const QString &key : keys)
                overrideMap[std::make_pair(dow, key)].append(override_);
        }
    }

    QJsonArray schedulesOut;
    for (const QJsonValue &dayValue : ScheduleModel::days(schedule)) {
        const QJsonObject day = dayValue.toObject();
        // day.dayOfWeek or [0]（converter.py:478）
        QList<int> dayDows;
        const QStringList dayDowStrings = ScheduleModel::dayOfWeekList(day.value(QStringLiteral("dayOfWeek")));
        if (dayDowStrings.isEmpty()) {
            dayDows.append(0);
        } else {
            for (const QString &dowString : dayDowStrings)
                dayDows.append(dowString.toInt());
        }

        QString dayWeeksStr;
        if (!weeksToCsesKey(day.value(QStringLiteral("weeks")), &dayWeeksStr))
            return false; // converter.py:479

        for (const int dow : dayDows) {
            // 基础课程列表（仅 class 条目，converter.py:482-496）
            QJsonArray baseClasses;
            const QJsonArray entries = day.value(QLatin1String("entries")).toArray();
            for (const QJsonValue &entryValue : entries) {
                const QJsonObject entry = entryValue.toObject();
                if (ScheduleModel::entryType(entry) != QLatin1String(ScheduleModel::kTypeClass))
                    continue;
                const QString subjectId = ScheduleModel::entrySubjectId(entry);
                QString subjectName;
                if (!subjectId.isEmpty() && subjectsMap.contains(subjectId))
                    subjectName = subjectsMap.value(subjectId).value(QStringLiteral("name")).toString();
                else
                    subjectName = QCoreApplication::translate("ScheduleConverter", "Class");
                QString startTime;
                QString endTime;
                if (!toCsesTime(ScheduleModel::entryStartTime(entry), &startTime, error))
                    return false; // converter.py:493
                if (!toCsesTime(ScheduleModel::entryEndTime(entry), &endTime, error))
                    return false;
                QJsonObject cls;
                cls.insert(QStringLiteral("subject"), subjectName);
                cls.insert(QStringLiteral("start_time"), startTime);
                cls.insert(QStringLiteral("end_time"), endTime);
                cls.insert(QStringLiteral("entry_id"), ScheduleModel::entryId(entry));
                baseClasses.append(cls);
            }

            // converter.py:498-506：周次候选
            const bool hasPerWeekOverride = overrideMap.contains(std::make_pair(dow, QStringLiteral("odd")))
                                            || overrideMap.contains(std::make_pair(dow, QStringLiteral("even")));
            QStringList weekCandidates;
            if (dayWeeksStr == QLatin1String("odd") || dayWeeksStr == QLatin1String("even")) {
                weekCandidates = { dayWeeksStr };
            } else if (dayWeeksStr == QLatin1String("all") && hasPerWeekOverride) {
                weekCandidates = { QStringLiteral("odd"), QStringLiteral("even") };
            } else {
                weekCandidates = { QStringLiteral("all") };
            }

            for (const QString &weekStr : weekCandidates) {
                QJsonArray finalClasses = baseClasses;
                for (int i = 0; i < finalClasses.size(); ++i) {
                    QJsonObject cls = finalClasses.at(i).toObject();
                    const QString entryId = cls.value(QStringLiteral("entry_id")).toString();

                    // 选最优 override：先精确周次键（优先级 1）再 "all"（0）
                    QJsonObject bestOverride;
                    bool hasBest = false;
                    int bestPriority = -1;
                    const QString weekKeys[2] = { weekStr, QStringLiteral("all") }; // converter.py:515
                    for (const QString &weekKey : weekKeys) {
                        const auto it = overrideMap.constFind(std::make_pair(dow, weekKey));
                        if (it == overrideMap.constEnd())
                            continue;
                        for (const QJsonValue &overrideValue : it.value()) {
                            const QJsonObject override_ = overrideValue.toObject();
                            if (ScheduleModel::overrideEntryId(override_) != entryId)
                                continue; // converter.py:517
                            const QString overrideSubjectId =
                                override_.value(QStringLiteral("subjectId")).toString();
                            if (overrideSubjectId.isEmpty() || !subjectsMap.contains(overrideSubjectId))
                                continue;
                            const int priority = weekKey == weekStr ? 1 : 0; // converter.py:520
                            if (priority > bestPriority) {
                                bestOverride = override_;
                                hasBest = true;
                                bestPriority = priority;
                            }
                        }
                    }
                    if (hasBest) {
                        // converter.py:525-526：用 override 指向的学科名覆盖
                        cls.insert(QStringLiteral("subject"),
                                   subjectsMap.value(bestOverride.value(QStringLiteral("subjectId")).toString())
                                       .value(QStringLiteral("name")));
                    }
                    finalClasses[i] = cls;
                }

                // classes_out：去掉 entry_id（converter.py:529-532）
                QJsonArray classesOut;
                for (const QJsonValue &clsValue : finalClasses) {
                    const QJsonObject cls = clsValue.toObject();
                    QJsonObject outCls;
                    outCls.insert(QStringLiteral("subject"), cls.value(QStringLiteral("subject")));
                    outCls.insert(QStringLiteral("start_time"), cls.value(QStringLiteral("start_time")));
                    outCls.insert(QStringLiteral("end_time"), cls.value(QStringLiteral("end_time")));
                    classesOut.append(outCls);
                }

                const QString dayName = localizedDayName(dow) + QStringLiteral(" - ")
                                        + localizedWeekLabel(weekStr); // converter.py:534-535
                QJsonObject sch;
                sch.insert(QStringLiteral("name"), dayName);
                sch.insert(QStringLiteral("enable_day"), dow);
                sch.insert(QStringLiteral("weeks"), weekStr);
                sch.insert(QStringLiteral("classes"), classesOut);
                schedulesOut.append(sch);
            }
        }
    }

    // subjects（converter.py:544-554）：非空字符串字段才输出
    QJsonArray subjectsOut;
    for (const QJsonValue &subjectValue : subjectArray) {
        const QJsonObject subject = subjectValue.toObject();
        QJsonObject outSubject;
        outSubject.insert(QStringLiteral("name"), subject.value(QStringLiteral("name")));
        const QString simplified = subject.value(QStringLiteral("simplifiedName")).toString();
        if (!simplified.isEmpty())
            outSubject.insert(QStringLiteral("simplified_name"), simplified);
        const QString teacher = subject.value(QStringLiteral("teacher")).toString();
        if (!teacher.isEmpty())
            outSubject.insert(QStringLiteral("teacher"), teacher);
        const QString location = subject.value(QStringLiteral("location")).toString();
        if (!location.isEmpty())
            outSubject.insert(QStringLiteral("room"), location);
        subjectsOut.append(outSubject);
    }

    QJsonObject result;
    result.insert(QStringLiteral("version"), kCsesSchemaVersion); // converter.py:557
    result.insert(QStringLiteral("subjects"), subjectsOut);
    result.insert(QStringLiteral("schedules"), schedulesOut);
    *outCses = result;
    return true;
}

bool convertToCw2File(const QString &sourcePath, const QString &sourceFormat,
                      const QString &destPath, QString *error)
{
    // converter.py:563-578 to_cw2
    QJsonObject data;
    if (sourceFormat == QLatin1String("cses")) {
        if (!loadCses(sourcePath, &data, error))
            return false;
    } else if (sourceFormat == QLatin1String("cw1")) {
        if (!loadCw1(sourcePath, &data, error))
            return false;
    } else {
        *error = QStringLiteral(
            "Current data is not in a supported import format, cannot export to CW2.");
        cwn::Log::error(*error); // converter.py:565-566
        return false;
    }

    QJsonObject schedule;
    const bool ok = sourceFormat == QLatin1String("cses")
                        ? csesToCw2(data, &schedule, error)
                        : cw1ToCw2(data, &schedule, error);
    if (!ok) {
        cwn::Log::error(QStringLiteral("Failed to export to CW2: %1").arg(*error)); // 576-577
        return false;
    }

    if (!writeTextFile(destPath, QJsonDocument(schedule).toJson(QJsonDocument::Indented), error))
        return false;
    // 注：上游 json.dump(..., indent=2)；QJsonDocument::Indented 为 4 空格，
    // 仅空白差异，语义一致。
    cwn::Log::info(QStringLiteral("Converted to CW2 JSON: %1").arg(destPath)); // converter.py:574
    return true;
}

bool exportToCsesFile(const QString &cw2Path, const QString &destPath, QString *error)
{
    // converter.py:580-592 to_cses
    QJsonObject schedule;
    if (!loadCw2(cw2Path, &schedule, error))
        return false;

    QJsonObject cses;
    if (!cw2ToCses(schedule, &cses, error)) {
        cwn::Log::error(QStringLiteral("Failed to export to CSES: %1").arg(*error));
        return false;
    }

    if (!writeTextFile(destPath, dumpCsesYaml(cses), error))
        return false;
    cwn::Log::info(QStringLiteral("Converted to CSES YAML: %1").arg(destPath)); // converter.py:588
    return true;
}

} // namespace ScheduleConverter
