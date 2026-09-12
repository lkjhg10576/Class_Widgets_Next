#pragma once

#include <QDate>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QTime>
#include <QVariant>

// 课表数据模型辅助层。
//
// 上游用 pydantic 模型（core/schedule/model.py）承载课表：ScheduleData
// = {meta, subjects[], days[], overrides[]}，其中
//   meta      = {id, version, maxWeekCycle, startDate}
//   subject   = {id, name, simplifiedName, teacher, icon, color, location, isLocalClassroom}
//   timeline  = {id, entries[], dayOfWeek(int|list|null), weeks("all"|int|list|null), date}
//   entry     = {id, type, startTime, endTime, subjectId, title}
//   override  = {id, entryId, dayOfWeek, weeks, subjectId, title, startTime, endTime}
//   type ∈ {"class","break","activity","free","preparation"}（model.py EntryType）
//
// 本移植用 QJsonObject 原样承载（与 model_dump() 输出逐字段对齐，含 null）。
// 所有固定访问路径都通过本命名空间的辅助函数读写，避免在业务代码里散落
// 裸字符串键；normalize* 系列负责把磁盘上缺省的可选字段补齐成
// pydantic model_dump 的形状（QML 依赖字段存在性，如 isLocalClassroom）。
namespace ScheduleModel {

// 对应 src/__init__.py:9 __SCHEDULE_SCHEMA_VERSION__
inline constexpr int kSchemaVersion = 1;

// 条目类型字符串（model.py:8-13 EntryType）
inline constexpr const char *kTypeClass = "class";
inline constexpr const char *kTypeBreak = "break";
inline constexpr const char *kTypeActivity = "activity";
inline constexpr const char *kTypeFree = "free";
inline constexpr const char *kTypePreparation = "preparation";

// ── 基础工具（对应 core/utils）───────────────────────────────

// utils/__init__.py:15 generate_id：`prefix_hex32`
QString generateId(const QString &prefix);

// utils/calculator.py:4 get_week_number：开学后的第几周（开学前为负，不产生 0 周）
int weekNumber(const QString &startDate, const QDate &date);

// utils/calculator.py:20 get_cycle_week：周期内第几周。
// 注意 Python 的负数取模语义与 C++ 不同，这里显式归一化。
int cycleWeek(int weekNumber, int cycle);

// "HH:MM" 解析（对应 datetime.strptime(x, "%H:%M")；失败返回 invalid QTime）
QTime parseHm(const QString &text);

// QVariant → QJsonValue（QML 传参用）：invalid/null 统一为 JSON null，
// 列表/映射分别转数组/对象。等价于 editor.py:15 _jsvalue_to_python 的归一化目标。
QJsonValue toJsonValue(const QVariant &value);

// QML/JSON 传入的日期参数（"yyyy-MM-dd" 字符串或 JS Date）归一化为 ISO 日期串
QString dateParamToIso(const QVariant &value);

// ── 固定访问路径 ─────────────────────────────────────────────

QJsonObject meta(const QJsonObject &schedule);
QJsonArray subjects(const QJsonObject &schedule);
QJsonArray days(const QJsonObject &schedule);
QJsonArray overrides(const QJsonObject &schedule);

QString metaId(const QJsonObject &meta);
int metaVersion(const QJsonObject &meta);
int metaMaxWeekCycle(const QJsonObject &meta); // 缺省 1
QString metaStartDate(const QJsonObject &meta);

// schedule.meta.maxWeekCycle（缺省 1 的便捷形式，service.py:36）
int maxWeekCycle(const QJsonObject &schedule);
QString startDate(const QJsonObject &schedule);

QString entryId(const QJsonObject &entry);
QString entryType(const QJsonObject &entry);
QString entryStartTime(const QJsonObject &entry);
QString entryEndTime(const QJsonObject &entry);
QString entrySubjectId(const QJsonObject &entry);
QString entryTitle(const QJsonObject &entry);
QString overrideEntryId(const QJsonObject &override_);

// dayOfWeek 归一化：int → [int]；null/缺失/空 → 空列表（service.py:51 的语义）
QStringList dayOfWeekList(const QJsonValue &value);

// weeks 判断（service.py:189-210 _is_in_week）：
//   null → true；"all" → true；int → 周期规则；list → 包含
bool isInWeek(const QJsonValue &weeks, int currentWeek, int maxWeekCycle);

// override 优先级（swapper.py:458-466 _get_override_priority /
// editor.py:481-495 的同款规则）：3=周次列表命中，2=int 周期命中，1=all/空，-1=不适用
int overridePriority(const QJsonValue &weeks, int weekOfCycle, int maxWeekCycle);

// override 是否作用于给定星期/周次（service.py:90-97 _override_applies）
bool overrideApplies(const QJsonObject &override_, int weekday, int currentWeek,
                     int maxWeekCycle);

// ── 归一化（对齐 pydantic model_dump 的字段形状）─────────────

QJsonObject normalizeSubject(QJsonObject subject);
QJsonObject normalizeEntry(QJsonObject entry);
QJsonObject normalizeDay(QJsonObject day);
QJsonObject normalizeOverride(QJsonObject override_);
QJsonObject normalizeMeta(QJsonObject meta);
QJsonObject normalizeSchedule(QJsonObject schedule);

// ── 构造 ─────────────────────────────────────────────────────

// utils/subjects.py DEFAULT_SUBJECTS（20 个默认学科，走 QCoreApplication::translate）
QJsonArray defaultSubjects();

// manager.py:21-30 _create_empty_schedule：默认 meta + 默认学科 + 空 days
QJsonObject makeEmptySchedule();

// 从 QJsonArray 提取科目 id → 科目对象的查找表（service.py:171 get_subject 语义）
QJsonObject findSubject(const QJsonArray &subjects, const QString &subjectId);

} // namespace ScheduleModel
