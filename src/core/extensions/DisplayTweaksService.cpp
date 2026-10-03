#include "DisplayTweaksService.h"

#include "Logger.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

DisplayTweaksService::DisplayTweaksService(QObject *parent)
    : QObject(parent)
{
}

QStringList DisplayTweaksService::excludedSubjects(const QString &jsonArray) const
{
    QStringList out;
    QSet<QString> seen;
    const QString trimmed = jsonArray.trimmed();
    if (trimmed.isEmpty() || trimmed == QLatin1String("[]"))
        return out;
    // 新版 JSON 数组形状（配置键 hide_excluded_subjects，见 ConfigStore 默认 "[]"）
    if (trimmed.startsWith(QLatin1Char('['))) {
        const QJsonDocument doc = QJsonDocument::fromJson(trimmed.toUtf8());
        if (doc.isArray()) {
            for (const QJsonValue &v : doc.array()) {
                const QString name = v.toString().trimmed();
                if (name.isEmpty() || seen.contains(name))
                    continue;
                seen.insert(name);
                out.append(name);
                if (out.size() >= 20)
                    break; // 上游约束：排除科目 ≤20，加号达上限禁用
            }
            return out;
        }
    }
    // 旧版逗号分隔兼容（上游 hide_excluded_lessons 语义）
    for (const QString &part : trimmed.split(QLatin1Char(','))) {
        const QString name = part.trimmed();
        if (name.isEmpty() || seen.contains(name))
            continue;
        seen.insert(name);
        out.append(name);
        if (out.size() >= 20)
            break;
    }
    return out;
}

bool DisplayTweaksService::healthCheck(bool widgetsFlowPresent, bool schedulePeekBarPresent)
{
    // 补丁检测语义转为健康自检：只检查挂载点存在性。QML 侧传入 findChild 结果；
    // 任一缺失记 warn 并黄条提示，不要求卸载/回滚（与上游 FEATURE_PATCH_SPECS 失败即阻断不同）。
    const bool ok = widgetsFlowPresent && schedulePeekBarPresent;
    const bool changed = (ok != m_healthy);
    m_healthy = ok;
    m_healthMessage = ok ? QString()
                         : QStringLiteral("DisplayTweaks health check failed: "
                                          "widgetsFlow=%1 schedulePeekBar=%2")
                               .arg(widgetsFlowPresent ? QStringLiteral("ok") : QStringLiteral("missing"),
                                    schedulePeekBarPresent ? QStringLiteral("ok") : QStringLiteral("missing"));
    if (!ok)
        cwn::Log::warn(QStringLiteral("DisplayTweaksService: %1").arg(m_healthMessage));
    if (changed)
        emit healthChanged();
    return ok;
}
