#include "ScheduleParser.h"

#include "../Logger.h"
#include "ScheduleModel.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>

ScheduleParser::ScheduleParser(const QString &path)
    : m_path(path)
{
}

bool ScheduleParser::validate(const QJsonObject &data)
{
    // schedule.py:22-30
    return data.contains(QLatin1String("meta"))
        && data.value(QLatin1String("meta")).isObject()
        && data.value(QLatin1String("meta")).toObject().contains(QLatin1String("version"))
        && data.value(QLatin1String("meta")).toObject().contains(QLatin1String("startDate"));
}

ScheduleParser::Status ScheduleParser::load(QJsonObject *outSchedule, QString *errorMessage) const
{
    // schedule.py:32-40（JsonLoader.load → json.loads）
    QFile file(m_path);
    if (!file.exists()) {
        // 对应 FileNotFoundError("Schedule File not found")
        if (errorMessage) {
            *errorMessage = QStringLiteral("Schedule File not found: %1").arg(m_path);
        }
        return Status::FileNotFound;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        cwn::Log::error(QStringLiteral("ScheduleParser: cannot open %1: %2")
                            .arg(m_path, file.errorString()));
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot open schedule file: %1").arg(m_path);
        }
        return Status::InvalidJson;
    }

    const QByteArray raw = file.readAll().trimmed();
    if (raw.isEmpty()) {
        // json_loader.py:37-38：空文件按 {} 处理（随后 validate 失败）
        if (errorMessage) {
            *errorMessage = QStringLiteral("Schedule file is empty: %1").arg(m_path);
        }
        return Status::InvalidSchema;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        // 对应 json.decoder.JSONDecodeError
        cwn::Log::error(QStringLiteral("ScheduleParser: JSON decode error in %1: %2")
                            .arg(m_path, parseError.errorString()));
        if (errorMessage) {
            *errorMessage = QStringLiteral("JSON Decode Error: %1").arg(parseError.errorString());
        }
        return Status::InvalidJson;
    }

    const QJsonObject data = doc.object();

    if (!validate(data)) {
        // schedule.py:42-43 "Invalid Schedule File"
        cwn::Log::error(QStringLiteral("ScheduleParser: invalid schedule file: %1").arg(m_path));
        if (errorMessage) {
            *errorMessage = QStringLiteral("Invalid Schedule File");
        }
        return Status::InvalidSchema;
    }

    // schedule.py:47-48 版本检查（ScheduleData.model_validate 的字段归一化
    // 由 ScheduleModel::normalizeSchedule 承担）
    const int version = ScheduleModel::metaVersion(ScheduleModel::meta(data));
    if (version != ScheduleModel::kSchemaVersion) {
        cwn::Log::error(QStringLiteral("ScheduleParser: unsupported schema version %1").arg(version));
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unsupported schema version: %1").arg(version);
        }
        return Status::UnsupportedVersion;
    }

    if (outSchedule) {
        *outSchedule = ScheduleModel::normalizeSchedule(data);
    }
    return Status::Ok;
}
