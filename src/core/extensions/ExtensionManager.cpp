#include "ExtensionManager.h"

#include "ConfigStore.h"
#include "Logger.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QVariantMap>

#include <optional>

namespace {
// 启用集合的唯一切入点：读写都经此键，ConfigStore::sanitize 保证其为字符串数组
const char kEnabledKey[] = "extensions.enabled";
// 阶段 B 天气迁移的契约常量（extensions-feature-plan §5 B3）。id 字符串与
// BuiltinWidgets/WeatherService::widgetTypeId() 及 definitions() 注册表一致
const char kWeatherWidgetId[] = "classwidgets.weather";
const char kWeatherExtensionId[] = "classwidgets.ext.weather";
// 全局城市键：JSON 字符串 {cityId,name,lat,lon,province,adcode,wcnKey}
// （字段形状与 widgets/weather.qml、WeatherService::cityFromJson 的解析契约核对）
const char kWeatherCityKey[] = "weather.city";
const char kPresetsKey[] = "preferences.widgets_presets";
} // namespace

ExtensionManager::ExtensionManager(ConfigStore *configs, QObject *parent)
    : QObject(parent)
    , m_configs(configs)
{
    // 一次性迁移放在构造末尾：AppCentral::initialize 在 configs->load() 之后
    // 才创建本对象，且接线（connectServices）与 QML 引擎均在其后，时序天然满足
    migrateLegacyWeatherConfig();
    migrateMoreSettingsConfig();
}

// 静态注册表。名称/描述走 QCoreApplication::translate（context "Extensions"，
// 中文源文本；阶段 E 补齐 .ts 多语言后未译语种回退中文）
QList<ExtensionManager::ExtensionDefinition> ExtensionManager::definitions()
{
    QList<ExtensionDefinition> defs;

    ExtensionDefinition weather;
    weather.id = QStringLiteral("classwidgets.ext.weather");
    weather.name = QCoreApplication::translate("Extensions", "天气");
    weather.icon = QStringLiteral("ic_fluent_weather_partly_cloudy_day_20_regular");
    weather.description =
        QCoreApplication::translate("Extensions", "城市天气与恶劣天气预警，配置收敛到本扩展页");
    weather.settingsPageQml = QStringLiteral("pages/settings/Extensions/Weather.qml");
    defs.append(weather);

    ExtensionDefinition rollCall;
    rollCall.id = QStringLiteral("classwidgets.ext.rollCall");
    rollCall.name = QCoreApplication::translate("Extensions", "随机点名");
    rollCall.icon = QStringLiteral("ic_fluent_people_team_20_regular");
    rollCall.description =
        QCoreApplication::translate("Extensions", "屏幕悬浮点名按钮，按名单与权重随机抽取学生");
    rollCall.settingsPageQml = QStringLiteral("pages/settings/Extensions/RollCall.qml");
    defs.append(rollCall);

    ExtensionDefinition schedulePeek;
    schedulePeek.id = QStringLiteral("classwidgets.ext.schedulePeek");
    schedulePeek.name = QCoreApplication::translate("Extensions", "课表速览");
    schedulePeek.icon = QStringLiteral("ic_fluent_calendar_ltr_20_regular");
    schedulePeek.description = QCoreApplication::translate(
        "Extensions", "小组件下方的当日课表缩写条，高亮当前课与下一课");
    schedulePeek.settingsPageQml = QStringLiteral("pages/settings/Extensions/SchedulePeek.qml");
    defs.append(schedulePeek);

    ExtensionDefinition displayTweaks;
    displayTweaks.id = QStringLiteral("classwidgets.ext.displayTweaks");
    displayTweaks.name = QCoreApplication::translate("Extensions", "显示与小组件增强");
    displayTweaks.icon = QStringLiteral("ic_fluent_puzzle_cube_piece_20_regular");
    displayTweaks.description = QCoreApplication::translate(
        "Extensions", "时间/倒数日动画、隐藏深度与顶部距离、特定课程不隐藏");
    displayTweaks.settingsPageQml = QStringLiteral("pages/settings/Extensions/DisplayTweaks.qml");
    defs.append(displayTweaks);

    ExtensionDefinition homework;
    homework.id = QStringLiteral("classwidgets.ext.homework");
    homework.name = QCoreApplication::translate("Extensions", "当日作业");
    homework.icon = QStringLiteral("ic_fluent_clipboard_task_20_regular");
    homework.description = QCoreApplication::translate(
        "Extensions", "下课时提醒课代表填写当日作业，右侧浮窗汇总与编辑");
    homework.settingsPageQml = QStringLiteral("pages/settings/Extensions/Homework.qml");
    defs.append(homework);

    return defs;
}

bool ExtensionManager::hasDefinition(const QString &id)
{
    for (const ExtensionDefinition &def : definitions()) {
        if (def.id == id)
            return true;
    }
    return false;
}

QStringList ExtensionManager::enabledIds() const
{
    QStringList ids;
    if (!m_configs)
        return ids;
    const auto value = m_configs->value(QLatin1String(kEnabledKey));
    if (value.has_value() && value->isArray()) {
        for (const QJsonValue &e : value->toArray()) {
            if (e.isString())
                ids.append(e.toString());
        }
    }
    return ids;
}

QVariantList ExtensionManager::extensions() const
{
    const QStringList enabled = enabledIds();
    QVariantList out;
    for (const ExtensionDefinition &def : definitions()) {
        QVariantMap item;
        item.insert(QStringLiteral("id"), def.id);
        item.insert(QStringLiteral("name"), def.name);
        item.insert(QStringLiteral("icon"), def.icon);
        item.insert(QStringLiteral("description"), def.description);
        item.insert(QStringLiteral("enabled"), enabled.contains(def.id));
        item.insert(QStringLiteral("hasSettings"), !def.settingsPageQml.isEmpty());
        item.insert(QStringLiteral("settingsPage"), def.settingsPageQml);
        out.append(item);
    }
    return out;
}

bool ExtensionManager::isEnabled(const QString &id) const
{
    return enabledIds().contains(id);
}

void ExtensionManager::setEnabled(const QString &id, bool enabled)
{
    if (!m_configs || !hasDefinition(id)) {
        cwn::Log::warn(QStringLiteral("ExtensionManager: unknown extension id: %1").arg(id));
        return;
    }

    const QStringList before = enabledIds();
    if (before.contains(id) == enabled)
        return; // 状态无变化：不写配置、不发信号（避免无谓的模型重建）

    QStringList after = before;
    if (enabled)
        after.append(id);
    else
        after.removeAll(id);
    after.sort(); // 固定排序：反复开关不改变数组顺序，configs.json diff 稳定

    // 经 ConfigStore::set 写回：走锁定检查（extensions.enabled 可被锁定，
    // 如插件 API 强制某扩展状态）；set 触发 dataChanged → 脏标记
    m_configs->set(QLatin1String(kEnabledKey), after);
    // 开关是用户显式操作的结果，立即落盘而非等 1 分钟自动保存定时器，
    // 保证「重启后状态保持」的验收语义（崩溃也不丢）
    m_configs->save();

    // 以落盘后的实际值为准判定是否变化：被锁定拒绝时 set 未生效，
    // 不能对 QML 谎报 toggled
    const bool nowEnabled = enabledIds().contains(id);
    if (nowEnabled != before.contains(id))
        emit extensionToggled(id, nowEnabled);
    emit extensionsChanged();
}

// 存量天气配置一次性迁移（extensions-feature-plan §5 B3）。
// 幂等判据：weather.city 为非空字符串 → 迁移已完成直接返回。注意 ConfigStore
// 的 defaultConfig/sanitize 会把未迁移用户的 weather.city 补成空串 ""，
// 因此判据必须是「存在且非空」而非「键存在」。
// 迁移内容：
//   1) 任一预设含 classwidgets.weather 实例 → 启用 classwidgets.ext.weather
//      （老用户升级后天气小组件不丢失）；
//   2) 首个非空实例 settings.city → 写入全局 weather.city；
//      实例上的旧键保留不删（无害残留，且避免绕过 WidgetsModel 直写预设结构）。
// 读写全部经 ConfigStore（配置层合规迁移，不触碰运行期模型）；QJsonObject
// 的键按字典序遍历，多次启动扫描顺序稳定，"首个非空城市"结果幂等。
void ExtensionManager::migrateLegacyWeatherConfig()
{
    if (!m_configs)
        return;

    if (const auto city = m_configs->value(QLatin1String(kWeatherCityKey));
        city.has_value() && city->isString() && !city->toString().isEmpty())
        return; // 幂等闸门：已迁移（或用户已在扩展页配置过城市）

    const auto presetsValue = m_configs->value(QLatin1String(kPresetsKey));
    if (!presetsValue.has_value() || !presetsValue->isObject())
        return; // 新用户无预设数据：无事可迁，extensions.enabled 保持原样

    bool hasWeatherInstance = false;
    QString firstCity;
    const QJsonObject presets = presetsValue->toObject();
    for (auto it = presets.constBegin(); it != presets.constEnd(); ++it) {
        if (!it.value().isArray())
            continue;
        for (const QJsonValue &entryValue : it.value().toArray()) {
            if (!entryValue.isObject())
                continue;
            const QJsonObject entry = entryValue.toObject();
            if (entry.value(QLatin1String("type_id")).toString()
                != QLatin1String(kWeatherWidgetId))
                continue;
            hasWeatherInstance = true;
            if (firstCity.isEmpty()) {
                // 形状已由 ConfigStore::sanitize 的 normalizeWidgetPresets 规范
                // （settings 必为对象），此处再防御一次手改坏数据
                const QString city = entry.value(QLatin1String("settings"))
                                         .toObject()
                                         .value(QLatin1String("city"))
                                         .toString();
                if (!city.isEmpty())
                    firstCity = city;
            }
        }
    }

    if (!hasWeatherInstance)
        return; // 无存量天气：不启用扩展，也不改动任何键

    if (!firstCity.isEmpty())
        m_configs->set(QLatin1String(kWeatherCityKey), firstCity);

    QStringList after = enabledIds();
    const bool alreadyEnabled = after.contains(QLatin1String(kWeatherExtensionId));
    if (!alreadyEnabled) {
        after.append(QLatin1String(kWeatherExtensionId));
        after.sort(); // 与 setEnabled 相同的固定排序：configs.json diff 稳定
        m_configs->set(QLatin1String(kEnabledKey), after);
        // 与 setEnabled 相同的立即落盘语义：迁移结果不因未等到自动保存而丢失
        m_configs->save();
    }

    // 不发 extensionToggled/extensionsChanged：此刻接线与 QML 监听均未建立，
    // 启动期实例加载（WidgetsModel::loadConfig）本就会从配置读到天气实例；
    // QML 侧开关状态由 extensions() 现读配置，首绑即正确。
    cwn::Log::info(
        QStringLiteral("ExtensionManager: legacy weather migrated (city=%1, extension=%2)")
            .arg(firstCity.isEmpty() ? QStringLiteral("none found")
                                     : QStringLiteral("weather.city written"),
                 alreadyEnabled ? QStringLiteral("already enabled")
                                : QStringLiteral("auto-enabled")));
}

// four-plugins A2：More_settings 配置迁移（一次性，只提升不删除，源键保留）。
// 幂等闸门（质检修正）：以 extensions.display_tweaks.migrated 专用标记键判定，
// 迁移执行一次后置 true——原实现靠目标键"非默认痕迹"判定，存量用户旧插件配置
// 为全默认（最常见的科目列表空 + 并排标题形态）时痕迹不存在，每次启动都会用
// 源键重写目标键，把用户在设置页的调参静默回滚。标记键缺席（首次升级）才迁移。
void ExtensionManager::migrateMoreSettingsConfig()
{
    if (!m_configs)
        return;
    const auto migrated = m_configs->value(QStringLiteral("extensions.display_tweaks.migrated"));
    if (migrated.has_value() && migrated->toBool())
        return; // 幂等闸门：已迁移（或用户已确认过目标态），永不回写
    const auto srcValue = m_configs->value(QStringLiteral("plugins.configs.com.kryon.more_settings"));
    if (!srcValue.has_value() || !srcValue->isObject()) {
        // 无上游插件配置也置标记：目标键保持默认树值，避免每次启动重复判定
        m_configs->set(QStringLiteral("extensions.display_tweaks.migrated"), true);
        m_configs->save();
        return;
    }
    const QJsonObject src = srcValue->toObject();
    if (src.isEmpty()) {
        m_configs->set(QStringLiteral("extensions.display_tweaks.migrated"), true);
        m_configs->save();
        return;
    }
    // （补充质检修正）首轮修复遗留的"目标非默认痕迹"早退分支已整体移除：
    // 标记键是唯一闸门——痕迹判定在用户日后把目标键改回默认值时会重新放行，
    // 留下旧插件源覆盖用户调参的窄域回滚窗口

    const auto readBool = [&](const char *key, bool fallback) {
        const QJsonValue v = src.value(QLatin1String(key));
        if (v.isBool())
            return v.toBool();
        if (v.isString()) {
            if (v.toString().compare(QLatin1String("true"), Qt::CaseInsensitive) == 0)
                return true;
            if (v.toString().compare(QLatin1String("false"), Qt::CaseInsensitive) == 0)
                return false;
        }
        if (v.isDouble())
            return v.toInt() != 0;
        return fallback;
    };
    const auto readInt = [&](const char *key, int fallback) {
        const QJsonValue v = src.value(QLatin1String(key));
        if (v.isDouble())
            return v.toInt();
        if (v.isString()) {
            bool ok = false;
            const int i = v.toString().toInt(&ok);
            if (ok)
                return i;
        }
        if (v.isBool())
            return v.toBool() ? 1 : 0;
        return fallback;
    };

    m_configs->set(QStringLiteral("extensions.display_tweaks.countdown_animation"),
                   readBool("countdown_animation", true));
    m_configs->set(QStringLiteral("extensions.display_tweaks.time_animation"),
                   readBool("time_animation", true));
    m_configs->set(QStringLiteral("extensions.display_tweaks.time_show_seconds"),
                   readBool("time_show_seconds", true));
    m_configs->set(QStringLiteral("extensions.display_tweaks.time_show_date"),
                   readBool("time_show_date", true));
    m_configs->set(QStringLiteral("extensions.display_tweaks.time_show_year"),
                   readBool("time_show_year", true));
    m_configs->set(QStringLiteral("extensions.display_tweaks.time_show_month"),
                   readBool("time_show_month", true));
    m_configs->set(QStringLiteral("extensions.display_tweaks.time_show_day"),
                   readBool("time_show_day", true));
    m_configs->set(QStringLiteral("extensions.display_tweaks.time_show_weekday"),
                   readBool("time_show_weekday", true));
    const QString titleMode = src.value(QLatin1String("time_title_mode")).toString();
    m_configs->set(QStringLiteral("extensions.display_tweaks.time_title_mode"),
                   titleMode == QLatin1String("alternate") ? QStringLiteral("alternate")
                                                           : QStringLiteral("side_by_side"));
    m_configs->set(QStringLiteral("extensions.display_tweaks.time_alternate_interval"),
                   qBound(500, readInt("time_alternate_interval", 3000), 30000));
    m_configs->set(QStringLiteral("extensions.display_tweaks.time_alternate_animation"),
                   readBool("time_alternate_animation", false));
    m_configs->set(QStringLiteral("extensions.display_tweaks.display_height"),
                   src.value(QLatin1String("display_height")).isDouble()
                       ? qBound(-1, int(src.value(QLatin1String("display_height")).toDouble()), 500)
                       : readInt("display_height", -1));
    m_configs->set(QStringLiteral("extensions.display_tweaks.hide_depth"),
                   qBound(-1, readInt("hide_depth", -1), 200)); // -1=跟随平台默认
    m_configs->set(QStringLiteral("extensions.display_tweaks.hide_excluded_enabled"),
                   readBool("hide_excluded_enabled", false));
    // 科目列表：新版 JSON 数组优先，旧版逗号分隔回退
    QString subjects = src.value(QLatin1String("hide_excluded_subjects")).toString();
    if (subjects.isEmpty() || subjects == QLatin1String("[]")) {
        const QString legacy = src.value(QLatin1String("hide_excluded_lessons")).toString().trimmed();
        if (!legacy.isEmpty()) {
            QJsonArray arr;
            for (const QString &part : legacy.split(QLatin1Char(','))) {
                const QString name = part.trimmed();
                if (!name.isEmpty())
                    arr.append(name);
            }
            subjects = QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
            if (subjects.isEmpty())
                subjects = QStringLiteral("[]");
        } else {
            subjects = QStringLiteral("[]");
        }
    }
    m_configs->set(QStringLiteral("extensions.display_tweaks.hide_excluded_subjects"), subjects);
    m_configs->set(QStringLiteral("extensions.display_tweaks.migrated"), true);
    m_configs->save();
    cwn::Log::info(QStringLiteral("ExtensionManager: more_settings migrated to display_tweaks"));
}
