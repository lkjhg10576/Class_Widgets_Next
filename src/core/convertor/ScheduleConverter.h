#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

// 对应上游 core/convertor/converter.py（599 行）ScheduleConverter：
// 课表格式转换核心，CW2 内部格式（JSON）↔ CSES（通用课表交换标准，YAML）↔
// CW1（Class Widgets 1 的 JSON）。注意 "cw2" 在这里是课表 JSON 格式名而非项目名。
//
// 上游以实例承载 (data, source_format) 后调用各转换方法；本移植把同名方法
// 落为命名空间内的纯函数，错误统一经 QString *error 传回（上游靠异常），
// 调用方（ConvertorBridge）失败时返回 false，QML 按失败分支弹提示，与上游
// slots.py 的返回值语义一致。
//
// ── YAML 结论 ────────────────────────────────────────────────────────────
// 上游 CSES 使用 PyYAML：读取 yaml.safe_load（converter.py:75）、写出
// yaml.safe_dump(..., allow_unicode=True, sort_keys=False)（converter.py:587）。
// CSES 的数据形状只有 dict / list / str / int / null，与 JSON 同构，因此：
//   · 导出：实现 PyYAML safe_dump 风格的块级 YAML 发射器（映射/序列/标量，
//     中文按 allow_unicode 直接输出；歧义标量加单引号），无第三方库；
//   · 导入：实现"块级 YAML 子集"解析器，文档为纯 JSON（流式风格）时直接
//     走 QJsonDocument。局限：不支持锚点/别名/标签/多行块标量(| >)/多文档/
//     八进制·十六进制·性别数等隐式标量，仅覆盖 CSES 所需子集（见 .cpp 注释）。
namespace ScheduleConverter {

// 对应 src/__init__.py:10 __CSES_SCHEMA_VERSION__
inline constexpr int kCsesSchemaVersion = 1;

// ── 文件加载 + 校验（converter.py:71-117 from_* 与 123-140 _validate）──

// from_cses：读取 CSES YAML 文本并校验 required keys / schema version
bool loadCses(const QString &path, QJsonObject *outCses, QString *error);
// from_cw2：读取 CW2 JSON 课表并校验 meta.version
bool loadCw2(const QString &path, QJsonObject *outSchedule, QString *error);
// from_cw1：读取 CW1 JSON 课表并校验 required keys
bool loadCw1(const QString &path, QJsonObject *outCw1, QString *error);

// ── 转换核心 ─────────────────────────────────────────────────────────────

// converter.py:381-445 _convert_cses_to_cw2；输出为 model_dump 形状的课表
bool csesToCw2(const QJsonObject &cses, QJsonObject *outSchedule, QString *error);
// converter.py:341-378 _convert_cw1_to_cw2；输出为 model_dump 形状的课表
bool cw1ToCw2(const QJsonObject &cw1, QJsonObject *outSchedule, QString *error);
// converter.py:448-560 _convert_cw2_to_cses；输入为课表对象
bool cw2ToCses(const QJsonObject &schedule, QJsonObject *outCses, QString *error);

// ── 文件级编排（converter.py:563-592 to_cw2 / to_cses）──────────────────

// to_cw2：sourceFormat ∈ {"cses", "cw1"}，转换并写出 CW2 JSON 到 destPath
bool convertToCw2File(const QString &sourcePath, const QString &sourceFormat,
                      const QString &destPath, QString *error);
// to_cses：读取 CW2 JSON 课表并导出 CSES YAML 到 destPath
bool exportToCsesFile(const QString &cw2Path, const QString &destPath, QString *error);

// ── 本地化标签（converter.py:54-68，CSES 的 schedule.name 用）───────────

// get_localized_day_name：QLocale 长格式星期名（dow ∈ 1~7）
QString localizedDayName(int dow);
// get_localized_week_label："all"/"odd"/"even" 的本地化标签
QString localizedWeekLabel(const QString &weekStr);

// ── CSES 文本读写（YAML 子集，见文件头结论）────────────────────────────

// 解析 CSES 文档：纯 JSON（流式风格）走 QJsonDocument，否则按块级 YAML 子集解析
bool parseCsesDocument(const QByteArray &text, QJsonObject *outCses, QString *error);
// 生成 PyYAML safe_dump 风格（allow_unicode、sort_keys=False）的块级 YAML
QByteArray dumpCsesYaml(const QJsonObject &cses);

} // namespace ScheduleConverter
