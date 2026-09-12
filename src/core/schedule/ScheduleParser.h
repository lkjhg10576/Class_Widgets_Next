#pragma once

#include <QJsonObject>
#include <QString>

// 对应上游 core/parser/schedule.py ScheduleParser（53 行）。
// 职责：读取课表 JSON 文件 → 结构校验 → schema 版本检查。
// 上游通过异常传递错误；这里用 Status 枚举 + errorMessage 表达，语义一一对应：
//   FileNotFoundError            → FileNotFound
//   json.decoder.JSONDecodeError → InvalidJson
//   "Invalid Schedule File"      → InvalidSchema
//   "Unsupported schema version" → UnsupportedVersion
class ScheduleParser
{
public:
    enum class Status
    {
        Ok,
        FileNotFound,
        InvalidJson,
        InvalidSchema,
        UnsupportedVersion,
    };

    explicit ScheduleParser(const QString &path);

    // schedule.py:22-30 validate：必须是 dict 且 meta.version / meta.startDate 存在
    static bool validate(const QJsonObject &data);

    // schedule.py:32-52 load：成功时 *outSchedule 为归一化后的课表对象；
    // 失败时 *errorMessage 给出可读原因。两者可传 nullptr。
    Status load(QJsonObject *outSchedule, QString *errorMessage) const;

private:
    QString m_path;
};
