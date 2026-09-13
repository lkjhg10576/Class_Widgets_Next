#include "ConfigStore.h"

#include "Logger.h"

#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLocale>
#include <QVariant>

#include <cmath>
#include <optional>

namespace {
// 对应上游 src/__init__.py 的 __version__ / __version_type__
// （保持与上游一致，避免存量 configs.json 触发版本不匹配分支）
const char kAppVersion[] = "2.0.0.dev31546377";
const char kAppChannel[] = "alpha";
// 对应 model.py:185 PreferencesConfig.font 的默认值
const char kDefaultFont[] = "Microsoft YaHei";

// ────────────────────────── 类型校验表 ──────────────────────────
// 轻量手写替代 Pydantic（model.py 各字段的 int/float/bool/str/枚举约束）。
// 仅约束已知的标量叶子；dict/list 容器由 sanitize() 的结构规范化负责。

enum class ScalarKind {
    Bool,   // bool
    Int,    // int
    Float,  // float（JSON 数字统一为 double）
    Str,    // str
    Enum,   // 限定取值的 str（model.py:80-101 的四个枚举）
    OptStr, // Optional[str]（允许 null）
    OptInt, // Optional[int]（允许 null）
};

struct ScalarSpec {
    const char *path;                        // 点分路径
    ScalarKind kind;                         // 期望类型
    const char *const *enumValues = nullptr; // 仅 Enum 使用，nullptr 结尾
};

// model.py:80-86 LayoutAnchor
constexpr const char *kLayoutAnchors[] = { "top_left",    "top_center", "top_right",
                                           "bottom_left", "bottom_center",
                                           "bottom_right", nullptr };
// model.py:89-92 ZOrder
constexpr const char *kZOrder[] = { "top", "bottom", "normal", nullptr };
// model.py:94-96 CountdownPrecision
constexpr const char *kCountdownPrecision[] = { "second", "minute", nullptr };
// model.py:98-101 TapAction（hide 复用于 HideInteractionsConfig.action）
constexpr const char *kTapAction[] = { "hide", "mini_mode", "floating_widget", nullptr };

// 各分区注释标注对应 model.py 的模型类与行号
const ScalarSpec kScalarSpecs[] = {
    // AppConfig（model.py:137-146）
    { "app.debug_mode", ScalarKind::Bool },
    { "app.no_logs", ScalarKind::Bool },
    { "app.version", ScalarKind::Str },
    { "app.channel", ScalarKind::Str },
    { "app.tutorial_completed", ScalarKind::Bool },
    { "app.auto_startup", ScalarKind::Bool },
    // LocaleConfig（model.py:108-112）
    { "locale.language", ScalarKind::Str },
    // ScheduleConfig（model.py:228-234）+ ScheduleDefaultDurationConfig（114-120）
    { "schedule.current_schedule", ScalarKind::Str },
    { "schedule.preparation_time", ScalarKind::Int },
    { "schedule.time_offset", ScalarKind::Int },
    { "schedule.default_duration.class_", ScalarKind::Int },
    { "schedule.default_duration.break_", ScalarKind::Int },
    { "schedule.default_duration.activity", ScalarKind::Int },
    // PreferencesConfig（model.py:149-189）
    { "preferences.current_theme", ScalarKind::Str },
    { "preferences.scale_factor", ScalarKind::Float },
    { "preferences.opacity", ScalarKind::Float },
    { "preferences.widget_corner_radius", ScalarKind::Float },
    { "preferences.widgets_anchor", ScalarKind::Enum, kLayoutAnchors },
    { "preferences.widgets_offset_x", ScalarKind::Int },
    { "preferences.widgets_offset_y", ScalarKind::Int },
    { "preferences.widgets_layer", ScalarKind::Enum, kZOrder },
    { "preferences.display", ScalarKind::OptStr },
    { "preferences.mini_mode", ScalarKind::Bool },
    { "preferences.lighting_effect", ScalarKind::Bool },
    { "preferences.countdown_precision", ScalarKind::Enum, kCountdownPrecision },
    { "preferences.current_preset", ScalarKind::Str },
    { "preferences.font", ScalarKind::Str },
    { "preferences.font_weight", ScalarKind::Int },
    { "preferences.floating_widget_x", ScalarKind::OptInt },
    { "preferences.floating_widget_y", ScalarKind::OptInt },
    // InteractionsConfig（model.py:198-204）+ HideInteractionsConfig（122-131）
    { "interactions.hover_fade", ScalarKind::Bool },
    { "interactions.tapped_action", ScalarKind::Enum, kTapAction },
    { "interactions.hide.state", ScalarKind::Bool },
    { "interactions.hide.in_class", ScalarKind::Bool },
    { "interactions.hide.clicked", ScalarKind::Bool },
    { "interactions.hide.maximized", ScalarKind::Bool },
    { "interactions.hide.fullscreen", ScalarKind::Bool },
    { "interactions.hide.action", ScalarKind::Enum, kTapAction },
    // PluginsConfig（model.py:211-217）
    { "plugins.auto_check_plaza_updates", ScalarKind::Bool },
    { "plugins.auto_install_plaza_updates", ScalarKind::Bool },
    // NetworkConfig（model.py:237-247）
    { "network.current_mirror", ScalarKind::Str },
    { "network.mirror_enabled", ScalarKind::Bool },
    { "network.releases_url", ScalarKind::Str },
    { "network.auto_check_updates", ScalarKind::Bool },
    { "network.plaza_url", ScalarKind::Str },
    // NotificationsConfig（model.py:249-265）
    { "notifications.enabled", ScalarKind::Bool },
    { "notifications.default_sound", ScalarKind::OptStr },
    { "notifications.volume", ScalarKind::Float },
    { "notifications.default_duration", ScalarKind::Int },
};

const ScalarSpec *findScalarSpec(const QString &dottedKey)
{
    for (const ScalarSpec &spec : kScalarSpecs) {
        if (dottedKey == QLatin1String(spec.path))
            return &spec;
    }
    return nullptr;
}

// ────────────────────── 点分路径读写辅助 ──────────────────────
// 取点分路径的值；任一中间层缺失或不是对象时返回 Undefined
QJsonValue jsonGetAt(const QJsonObject &root, const QString &dottedKey)
{
    const QStringList parts = dottedKey.split(QLatin1Char('.'));
    if (parts.isEmpty() || parts.first().isEmpty())
        return QJsonValue(QJsonValue::Undefined);

    QJsonValue current(root);
    for (const QString &part : parts) {
        if (!current.isObject())
            return QJsonValue(QJsonValue::Undefined);
        current = current.toObject().value(part);
        if (current.isUndefined())
            return QJsonValue(QJsonValue::Undefined);
    }
    return current;
}

// 写点分路径；中间层缺失时自动补空对象（QJsonObject 无引用语义，
// 逐层取副本自底向上写入后整体回写根对象）
void jsonSetAt(QJsonObject &root, const QString &dottedKey, const QJsonValue &value)
{
    const QStringList parts = dottedKey.split(QLatin1Char('.'));
    if (parts.isEmpty() || parts.first().isEmpty())
        return;

    QList<QJsonObject> levels;
    levels.append(root);
    for (int i = 0; i < parts.size() - 1; ++i) {
        QJsonObject next = levels.last().value(parts.at(i)).toObject();
        levels.append(next);
    }

    QJsonValue newValue = value;
    for (int i = parts.size() - 1; i >= 0; --i) {
        QJsonObject obj = levels.at(i);
        obj.insert(parts.at(i), newValue);
        newValue = obj;
    }
    root = newValue.toObject();
}

// ────────────────────── 标量类型纠正（Pydantic 宽松模式） ──────────────────────
// 整数值的 JSON 数字（QJsonValue 数字统一按 double 存储）
bool isIntegralNumber(const QJsonValue &v)
{
    if (!v.isDouble())
        return false;
    const double d = v.toDouble();
    return std::floor(d) == d && d >= -2147483648.0 && d <= 2147483647.0;
}

// bool：接受 bool、0/1、"true"/"false"（对应 Pydantic v2 lax 模式）
std::optional<QJsonValue> coerceBool(const QJsonValue &v)
{
    if (v.isBool())
        return v;
    if (isIntegralNumber(v) && (v.toInt() == 0 || v.toInt() == 1))
        return QJsonValue(v.toInt() == 1);
    if (v.isString()) {
        const QString s = v.toString();
        if (s.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0 || s == QLatin1String("1"))
            return QJsonValue(true);
        if (s.compare(QLatin1String("false"), Qt::CaseInsensitive) == 0
            || s == QLatin1String("0"))
            return QJsonValue(false);
    }
    return std::nullopt;
}

// int：接受 bool（True→1）、整数值数字、可解析的数字字符串
std::optional<QJsonValue> coerceInt(const QJsonValue &v)
{
    if (v.isBool())
        return QJsonValue(v.toBool() ? 1 : 0);
    if (isIntegralNumber(v))
        return QJsonValue(v.toInt());
    if (v.isString()) {
        bool ok = false;
        const int i = v.toString().toInt(&ok);
        if (ok)
            return QJsonValue(i);
    }
    return std::nullopt;
}

// float：接受任意 JSON 数字、bool、可解析的数字字符串
std::optional<QJsonValue> coerceFloat(const QJsonValue &v)
{
    if (v.isDouble())
        return v;
    if (v.isBool())
        return QJsonValue(v.toBool() ? 1.0 : 0.0);
    if (v.isString()) {
        bool ok = false;
        const double d = v.toString().toDouble(&ok);
        if (ok)
            return QJsonValue(d);
    }
    return std::nullopt;
}

// str：字符串原样；数字/布尔转字符串（比 Pydantic 宽松，保数据不丢）
std::optional<QJsonValue> coerceString(const QJsonValue &v)
{
    if (v.isString())
        return v;
    if (v.isBool())
        return QJsonValue(v.toBool() ? QStringLiteral("true") : QStringLiteral("false"));
    if (isIntegralNumber(v))
        return QJsonValue(QString::number(v.toInt()));
    if (v.isDouble())
        return QJsonValue(QString::number(v.toDouble()));
    return std::nullopt;
}

// 枚举：字符串且取值在白名单内（model.py 的 str Enum 序列化为值字符串）
std::optional<QJsonValue> coerceEnum(const QJsonValue &v, const char *const *allowed)
{
    if (!v.isString())
        return std::nullopt;
    const QString s = v.toString();
    for (int i = 0; allowed[i] != nullptr; ++i) {
        if (s == QLatin1String(allowed[i]))
            return v;
    }
    return std::nullopt;
}

// 按字段规格纠正；无法纠正时返回 std::nullopt（调用方决定回退默认值或拒绝写入）
std::optional<QJsonValue> coerceScalar(const ScalarSpec &spec, const QJsonValue &v)
{
    switch (spec.kind) {
    case ScalarKind::Bool:
        return coerceBool(v);
    case ScalarKind::Int:
        return coerceInt(v);
    case ScalarKind::Float:
        return coerceFloat(v);
    case ScalarKind::Str:
        return coerceString(v);
    case ScalarKind::Enum:
        return coerceEnum(v, spec.enumValues);
    case ScalarKind::OptStr:
        if (v.isNull())
            return v;
        return coerceString(v);
    case ScalarKind::OptInt:
        if (v.isNull())
            return v;
        return coerceInt(v);
    }
    return std::nullopt;
}

// ────────────────────── 容器结构规范化 ──────────────────────
// 对应 model.py 的 list/dict 字段：形状不对时整体回退默认值，
// 元素类型不对时逐元素纠正（Pydantic 校验失败会丢弃整个配置，这里宽松保数据）。

// 数组元素规范为字符串；对象/数组元素丢弃
QJsonArray toStringArray(const QJsonArray &in, bool *changed)
{
    QJsonArray out;
    for (const QJsonValue &e : in) {
        const std::optional<QJsonValue> s = coerceString(e);
        if (s.has_value()) {
            if (!e.isString())
                *changed = true;
            out.append(*s);
        } else {
            *changed = true;
        }
    }
    return out;
}

bool normalizeStringList(QJsonObject &root, const QString &path, const QJsonObject &defaults)
{
    const QJsonValue current = jsonGetAt(root, path);
    if (!current.isArray()) {
        jsonSetAt(root, path, jsonGetAt(defaults, path));
        return true;
    }
    bool changed = false;
    const QJsonArray out = toStringArray(current.toArray(), &changed);
    if (!changed)
        return false;
    jsonSetAt(root, path, out);
    return true;
}

// 对象的每个值都必须是对象（plugins.configs / notifications.providers）
bool normalizeObjectOfObjects(QJsonObject &root, const QString &path,
                              const QJsonObject &defaults)
{
    const QJsonValue current = jsonGetAt(root, path);
    if (!current.isObject()) {
        jsonSetAt(root, path, jsonGetAt(defaults, path));
        return true;
    }
    const QJsonObject obj = current.toObject();
    bool changed = false;
    QJsonObject out;
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        if (it.value().isObject()) {
            out.insert(it.key(), it.value());
        } else {
            changed = true;
            out.insert(it.key(), QJsonObject());
        }
    }
    if (!changed)
        return false;
    jsonSetAt(root, path, out);
    return true;
}

// 数组的每个元素都必须是对象（plugins.pending_operations），非对象元素丢弃
bool normalizeArrayOfObjects(QJsonObject &root, const QString &path,
                             const QJsonObject &defaults)
{
    const QJsonValue current = jsonGetAt(root, path);
    if (!current.isArray()) {
        jsonSetAt(root, path, jsonGetAt(defaults, path));
        return true;
    }
    const QJsonArray arr = current.toArray();
    bool changed = false;
    QJsonArray out;
    for (const QJsonValue &e : arr) {
        if (e.isObject()) {
            out.append(e);
        } else {
            changed = true;
        }
    }
    if (!changed)
        return false;
    jsonSetAt(root, path, out);
    return true;
}

// 对象的每个值都必须是字符串（network.mirrors / notifications.level_sounds）
bool normalizeObjectOfStringValues(QJsonObject &root, const QString &path,
                                   const QJsonObject &defaults)
{
    const QJsonValue current = jsonGetAt(root, path);
    if (!current.isObject()) {
        jsonSetAt(root, path, jsonGetAt(defaults, path));
        return true;
    }
    const QJsonObject obj = current.toObject();
    bool changed = false;
    QJsonObject out;
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        const std::optional<QJsonValue> s = coerceString(it.value());
        if (s.has_value()) {
            if (!it.value().isString())
                changed = true;
            out.insert(it.key(), *s);
        } else {
            changed = true; // 对象/数组/null 值丢弃
        }
    }
    if (!changed)
        return false;
    jsonSetAt(root, path, out);
    return true;
}

// 必须是对象（schedule.reschedule_day / schedule.class_swap）
bool normalizePlainObject(QJsonObject &root, const QString &path, const QJsonObject &defaults)
{
    if (jsonGetAt(root, path).isObject())
        return false;
    jsonSetAt(root, path, jsonGetAt(defaults, path));
    return true;
}

// widgets_presets：{ preset 名: [ {type_id, instance_id, settings}, ... ] }
// （model.py:103-106 WidgetEntry：type_id 必填，缺 type_id 的条目丢弃）
bool normalizeWidgetPresets(QJsonObject &root, const QString &path, const QJsonObject &defaults)
{
    const QJsonValue current = jsonGetAt(root, path);
    if (!current.isObject()) {
        jsonSetAt(root, path, jsonGetAt(defaults, path));
        return true;
    }
    const QJsonObject obj = current.toObject();
    bool changed = false;
    QJsonObject presets;
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        if (!it.value().isArray()) {
            changed = true;
            presets.insert(it.key(), QJsonArray());
            continue;
        }
        QJsonArray outEntries;
        for (const QJsonValue &e : it.value().toArray()) {
            if (!e.isObject()) {
                changed = true;
                continue;
            }
            QJsonObject entry = e.toObject();
            if (!entry.value(QLatin1String("type_id")).isString()) {
                changed = true;
                continue; // type_id 缺失的条目无法定位小组件，丢弃
            }
            if (!entry.value(QLatin1String("instance_id")).isString()) {
                entry.insert(QLatin1String("instance_id"), QString());
                changed = true;
            }
            if (!entry.value(QLatin1String("settings")).isObject()) {
                entry.insert(QLatin1String("settings"), QJsonObject());
                changed = true;
            }
            outEntries.append(entry);
        }
        presets.insert(it.key(), outEntries);
    }
    if (!changed)
        return false;
    jsonSetAt(root, path, presets);
    return true;
}

// model.py:22-24 的默认 preset 条目构造（type_id + instance_id + 空 settings）
QJsonObject defaultWidgetPreset(const char *typeId, const char *instanceId)
{
    QJsonObject entry;
    entry.insert(QStringLiteral("type_id"), QString::fromLatin1(typeId));
    entry.insert(QStringLiteral("instance_id"), QString::fromLatin1(instanceId));
    entry.insert(QStringLiteral("settings"), QJsonObject());
    return entry;
}
} // namespace

// ────────────────────────── 构造与生命周期 ──────────────────────────

ConfigStore::ConfigStore(QString configsDir, QObject *parent)
    : QObject(parent)
    , m_configsDir(std::move(configsDir))
{
    m_saveTimer.setInterval(60 * 1000); // manager.py:48-49 save_timer 1 分钟
    connect(&m_saveTimer, &QTimer::timeout, this, [this] {
        if (m_dirty)
            save();
    });

    // dataChanged → 标脏，等待 1 分钟定时器或退出时落盘（上游无即时保存语义）
    connect(this, &ConfigStore::dataChanged, this, [this] { m_dirty = true; });
}

QString ConfigStore::filePath() const
{
    return m_configsDir + QStringLiteral("/configs.json");
}

// ────────────────────────── 默认配置树 ──────────────────────────
// 硬编码对齐 model.py 的全部字段默认值；字段名与 Pydantic model_dump_json 一致
// （class_/break_ 序列化后保留尾下划线，存量配置契约）。

QJsonObject ConfigStore::defaultConfig()
{
    QJsonObject app;
    app.insert(QStringLiteral("debug_mode"), false);        // model.py:141
    app.insert(QStringLiteral("no_logs"), false);           // model.py:142
    app.insert(QStringLiteral("version"), QString::fromLatin1(kAppVersion)); // 143
    app.insert(QStringLiteral("channel"), QString::fromLatin1(kAppChannel)); // 144
    app.insert(QStringLiteral("tutorial_completed"), false); // 145
    app.insert(QStringLiteral("auto_startup"), false);      // 146

    QJsonObject locale;
    locale.insert(QStringLiteral("language"), QLocale::system().name()); // model.py:112

    QJsonObject defaultDuration; // ScheduleDefaultDurationConfig（model.py:114-120）
    defaultDuration.insert(QStringLiteral("class_"), 40);
    defaultDuration.insert(QStringLiteral("break_"), 10);
    defaultDuration.insert(QStringLiteral("activity"), 30);

    QJsonObject schedule; // ScheduleConfig（model.py:228-234）
    // 上游默认值经 QCoreApplication.translate("Configs", ...)，M4 接入翻译后同形
    schedule.insert(QStringLiteral("current_schedule"),
                    QCoreApplication::translate("Configs", "New Schedule 1"));
    schedule.insert(QStringLiteral("preparation_time"), 2);
    schedule.insert(QStringLiteral("default_duration"), defaultDuration);
    schedule.insert(QStringLiteral("time_offset"), 0);
    schedule.insert(QStringLiteral("reschedule_day"), QJsonObject());
    schedule.insert(QStringLiteral("class_swap"), QJsonObject());

    QJsonArray shortcuts; // model.py:166-172
    shortcuts.append(QStringLiteral("com.classwidgets.settings"));
    shortcuts.append(QStringLiteral("com.classwidgets.schedules"));
    shortcuts.append(QStringLiteral("com.classwidgets.plugin-plaza"));
    shortcuts.append(QStringLiteral("com.classwidgets.reschedule-day"));
    shortcuts.append(QStringLiteral("com.classwidgets.class-swap"));

    QJsonArray defaultPreset; // model.py:174-182 widgets_presets 默认 preset
    defaultPreset.append(defaultWidgetPreset("classwidgets.time",
                                             "8ee721ef-ab36-4c23-834d-2c666a6739a3"));
    defaultPreset.append(defaultWidgetPreset("classwidgets.dynamicNotification",
                                             "4ccfdd24-eac1-4be0-8a09-7271af818327"));
    defaultPreset.append(defaultWidgetPreset("classwidgets.currentActivity",
                                             "87985398-2844-4c9e-b27d-6ea81cd0a2c6"));
    QJsonObject widgetsPresets;
    widgetsPresets.insert(QStringLiteral("default"), defaultPreset);

    QJsonObject preferences; // PreferencesConfig（model.py:149-189）
    preferences.insert(QStringLiteral("current_theme"),
                       QStringLiteral("com.classwidgets.default"));
    preferences.insert(QStringLiteral("scale_factor"), 1.0);
    preferences.insert(QStringLiteral("opacity"), 1.0);
    preferences.insert(QStringLiteral("widget_corner_radius"), 22.0);
    preferences.insert(QStringLiteral("widgets_anchor"), QStringLiteral("top_center"));
    preferences.insert(QStringLiteral("widgets_offset_x"), 0);
    preferences.insert(QStringLiteral("widgets_offset_y"), 24);
    preferences.insert(QStringLiteral("widgets_layer"), QStringLiteral("top"));
    preferences.insert(QStringLiteral("display"), QJsonValue::Null);
    preferences.insert(QStringLiteral("mini_mode"), false);
    preferences.insert(QStringLiteral("lighting_effect"), true);
    preferences.insert(QStringLiteral("countdown_precision"), QStringLiteral("second"));
    preferences.insert(QStringLiteral("shortcuts"), shortcuts);
    preferences.insert(QStringLiteral("widgets_presets"), widgetsPresets);
    preferences.insert(QStringLiteral("current_preset"), QStringLiteral("default"));
    preferences.insert(QStringLiteral("font"), QString::fromLatin1(kDefaultFont));
    preferences.insert(QStringLiteral("font_weight"), 600);
    preferences.insert(QStringLiteral("floating_widget_x"), QJsonValue::Null);
    preferences.insert(QStringLiteral("floating_widget_y"), QJsonValue::Null);

    QJsonObject hide; // HideInteractionsConfig（model.py:122-131）
    hide.insert(QStringLiteral("state"), false);
    hide.insert(QStringLiteral("in_class"), false);
    hide.insert(QStringLiteral("clicked"), true);
    hide.insert(QStringLiteral("maximized"), false);
    hide.insert(QStringLiteral("fullscreen"), false);
    hide.insert(QStringLiteral("action"), QStringLiteral("hide"));

    QJsonObject interactions; // InteractionsConfig（model.py:198-204）
    interactions.insert(QStringLiteral("hover_fade"), false);
    interactions.insert(QStringLiteral("hide"), hide);
    interactions.insert(QStringLiteral("tapped_action"), QStringLiteral("hide"));

    QJsonObject plugins; // PluginsConfig（model.py:211-217）
    QJsonArray enabledPlugins;
    enabledPlugins.append(QStringLiteral("builtin.classwidgets.widgets"));
    plugins.insert(QStringLiteral("enabled"), enabledPlugins);
    plugins.insert(QStringLiteral("configs"), QJsonObject());
    plugins.insert(QStringLiteral("pending_operations"), QJsonArray());
    plugins.insert(QStringLiteral("auto_check_plaza_updates"), true);
    plugins.insert(QStringLiteral("auto_install_plaza_updates"), false);

    QJsonObject mirrors; // model.py:17-21 GITHUB_MIRRORS
    mirrors.insert(QStringLiteral("gh_proxy"), QStringLiteral("https://gh-proxy.com/"));
    mirrors.insert(QStringLiteral("kkgithub"), QStringLiteral("https://kkgithub.com/"));
    mirrors.insert(QStringLiteral("gitfast"), QStringLiteral("https://gitfast.top/"));

    QJsonObject network; // NetworkConfig（model.py:237-247）
    network.insert(QStringLiteral("mirrors"), mirrors);
    network.insert(QStringLiteral("current_mirror"), QStringLiteral("gh_proxy"));
    network.insert(QStringLiteral("mirror_enabled"), true);
    network.insert(QStringLiteral("releases_url"),
                   QStringLiteral("https://classwidgets.rinlit.cn/2/releases.json"));
    network.insert(QStringLiteral("auto_check_updates"), true);
    network.insert(QStringLiteral("plaza_url"), QStringLiteral("https://plaza.cw.rinlit.cn"));

    QJsonObject levelSounds; // model.py:260-265 level_sounds（JSON 键为字符串）
    levelSounds.insert(QStringLiteral("0"), QString());
    levelSounds.insert(QStringLiteral("1"), QString());
    levelSounds.insert(QStringLiteral("2"), QString());
    levelSounds.insert(QStringLiteral("3"), QString());

    QJsonObject notifications; // NotificationsConfig（model.py:249-265）
    notifications.insert(QStringLiteral("enabled"), true);
    notifications.insert(QStringLiteral("default_sound"), QJsonValue::Null);
    notifications.insert(QStringLiteral("volume"), 0.7);
    notifications.insert(QStringLiteral("providers"), QJsonObject());
    notifications.insert(QStringLiteral("default_duration"), 8000);
    notifications.insert(QStringLiteral("level_sounds"), levelSounds);

    QJsonObject root; // manager.py:18-26 RootConfig
    root.insert(QStringLiteral("app"), app);
    root.insert(QStringLiteral("locale"), locale);
    root.insert(QStringLiteral("schedule"), schedule);
    root.insert(QStringLiteral("preferences"), preferences);
    root.insert(QStringLiteral("interactions"), interactions);
    root.insert(QStringLiteral("plugins"), plugins);
    root.insert(QStringLiteral("network"), network);
    root.insert(QStringLiteral("notifications"), notifications);
    return root;
}

QJsonObject ConfigStore::mergeDefaults(const QJsonObject &defaults, const QJsonObject &loaded)
{
    // 已加载的键优先，缺失的键用默认值补齐（近似 Pydantic 的字段缺省行为）；
    // 结构冲突（默认对象 vs 用户标量，或反之）时回退默认结构，
    // 避免下游 toObject() 得到空对象导致 QML 绑定崩溃
    QJsonObject merged = loaded;
    for (auto it = defaults.begin(); it != defaults.end(); ++it) {
        if (!merged.contains(it.key())) {
            merged.insert(it.key(), it.value());
            continue;
        }
        const QJsonValue user = merged.value(it.key());
        if (it.value().isObject() && user.isObject())
            merged.insert(it.key(), mergeDefaults(it.value().toObject(), user.toObject()));
        else if (it.value().isObject() != user.isObject())
            merged.insert(it.key(), it.value());
    }
    return merged;
}

// ────────────────────────── 载入后修正 ──────────────────────────

// 叶子类型校验/纠正 + 容器结构规范化（轻量替代 Pydantic 模型校验）
void ConfigStore::sanitize()
{
    const QJsonObject defaults = defaultConfig();
    int fixed = 0;

    // 1) 标量叶子：无法纠正时回退默认值（Pydantic 校验失败该字段取默认）
    for (const ScalarSpec &spec : kScalarSpecs) {
        const QString path = QLatin1String(spec.path);
        const QJsonValue current = jsonGetAt(m_json, path);
        if (current.isUndefined()) {
            jsonSetAt(m_json, path, jsonGetAt(defaults, path));
            ++fixed;
            continue;
        }
        std::optional<QJsonValue> corrected = coerceScalar(spec, current);
        if (!corrected.has_value())
            corrected = jsonGetAt(defaults, path);
        if (*corrected != current) {
            jsonSetAt(m_json, path, *corrected);
            ++fixed;
            cwn::Log::warn(QStringLiteral("Config field fixed: %1 (type mismatch)")
                               .arg(path));
        }
    }

    // 2) 容器结构（对应 model.py 各 list/dict 字段）
    if (normalizeStringList(m_json, QStringLiteral("preferences.shortcuts"), defaults))
        ++fixed;
    if (normalizeStringList(m_json, QStringLiteral("plugins.enabled"), defaults))
        ++fixed;
    if (normalizeObjectOfObjects(m_json, QStringLiteral("plugins.configs"), defaults))
        ++fixed;
    if (normalizeObjectOfObjects(m_json, QStringLiteral("notifications.providers"), defaults))
        ++fixed;
    if (normalizeArrayOfObjects(m_json, QStringLiteral("plugins.pending_operations"), defaults))
        ++fixed;
    if (normalizeObjectOfStringValues(m_json, QStringLiteral("network.mirrors"), defaults))
        ++fixed;
    if (normalizeObjectOfStringValues(m_json, QStringLiteral("notifications.level_sounds"),
                                      defaults))
        ++fixed;
    if (normalizePlainObject(m_json, QStringLiteral("schedule.reschedule_day"), defaults))
        ++fixed;
    if (normalizePlainObject(m_json, QStringLiteral("schedule.class_swap"), defaults))
        ++fixed;
    if (normalizeWidgetPresets(m_json, QStringLiteral("preferences.widgets_presets"), defaults))
        ++fixed;

    if (fixed > 0)
        cwn::Log::warn(QStringLiteral("Config sanitized: %1 field(s) corrected").arg(fixed));
}

// manager.py:73-92 _ensure_defaults：版本号对齐 + 字体回退
void ConfigStore::ensureDefaults()
{
    QJsonObject app = m_json.value(QStringLiteral("app")).toObject();
    if (app.value(QStringLiteral("version")).toString() != QString::fromLatin1(kAppVersion)
        || app.value(QStringLiteral("channel")).toString() != QString::fromLatin1(kAppChannel)) {
        cwn::Log::warn(QStringLiteral(
                            "Config version mismatch: %1 %2 != %3 %4, rewriting")
                            .arg(app.value(QStringLiteral("version")).toString(),
                                 app.value(QStringLiteral("channel")).toString(),
                                 QString::fromLatin1(kAppVersion),
                                 QString::fromLatin1(kAppChannel)));
        app.insert(QStringLiteral("version"), QString::fromLatin1(kAppVersion));
        app.insert(QStringLiteral("channel"), QString::fromLatin1(kAppChannel));
        m_json.insert(QStringLiteral("app"), app);
    }

    QJsonObject preferences = m_json.value(QStringLiteral("preferences")).toObject();
    if (preferences.value(QStringLiteral("font")).toString().isEmpty()) {
#ifdef Q_OS_WIN
        const QString fallback = QString::fromLatin1(kDefaultFont); // manager.py:89-90
#else
        const QString fallback =
            QFontDatabase::systemFont(QFontDatabase::GeneralFont).family(); // manager.py:92
#endif
        preferences.insert(QStringLiteral("font"), fallback);
        m_json.insert(QStringLiteral("preferences"), preferences);
    }
}

// manager.py:94-107 _clean_useless_configs：清理已过期（键日期早于今天）的
// reschedule_day 条目；ISO 日期字符串可直接按字典序比较
void ConfigStore::cleanUselessConfigs()
{
    const QJsonValue rescheduleValue =
        jsonGetAt(m_json, QStringLiteral("schedule.reschedule_day"));
    if (!rescheduleValue.isObject())
        return;
    const QJsonObject reschedule = rescheduleValue.toObject();
    const QString today = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));

    QStringList outdated;
    for (auto it = reschedule.constBegin(); it != reschedule.constEnd(); ++it) {
        if (today > it.key())
            outdated.append(it.key());
    }
    if (outdated.isEmpty())
        return;

    QJsonObject cleaned = reschedule;
    for (const QString &day : outdated)
        cleaned.remove(day);
    jsonSetAt(m_json, QStringLiteral("schedule.reschedule_day"), cleaned);
    cwn::Log::info(QStringLiteral("Cleaned useless configs: %1 expired reschedule_day entry(ies)")
                       .arg(outdated.size()));
}

// model.py:219-226 remove_legacy_plaza_sources：迁移期删除废弃的 plugins.plaza_sources
void ConfigStore::removeLegacyPlazaSources()
{
    QJsonObject plugins = m_json.value(QStringLiteral("plugins")).toObject();
    if (!plugins.contains(QStringLiteral("plaza_sources")))
        return;
    plugins.remove(QStringLiteral("plaza_sources"));
    m_json.insert(QStringLiteral("plugins"), plugins);
    cwn::Log::info(QStringLiteral("Removed legacy plugins.plaza_sources config"));
}

// ────────────────────────── 读写 ──────────────────────────

void ConfigStore::load()
{
    // 对应 load_config()（manager.py:109-122）。
    // 默认树兜底在前：文件存在但打不开（占用/权限）时也绝不落得空配置
    m_json = defaultConfig();

    QFile file(filePath());
    if (file.exists()) {
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QJsonParseError parseError;
            const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
            file.close();
            if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
                m_json = mergeDefaults(defaultConfig(), doc.object());
                cwn::Log::info(QStringLiteral("Loaded config: %1").arg(filePath()));
            } else {
                cwn::Log::warn(QStringLiteral("Load config failed: %1, use default config")
                                   .arg(parseError.errorString()));
                m_json = defaultConfig();
            }
        } else {
            cwn::Log::error(QStringLiteral("Load config failed: cannot open %1: %2, "
                                           "use default config")
                                .arg(filePath(), file.errorString()));
        }
    } else {
        cwn::Log::info(QStringLiteral("No config found, created default config at %1")
                           .arg(filePath()));
    }

    removeLegacyPlazaSources(); // Pydantic 迁移校验器（model.py:219-226）
    ensureDefaults();           // manager.py:116
    sanitize();                 // 轻量替代 Pydantic 模型校验
    cleanUselessConfigs();      // manager.py:117
    save();                     // manager.py:122：load 后无条件 save
    invalidateDataCache();      // load 及其修正函数直写 m_json，统一失效缓存
}

void ConfigStore::save(bool silent)
{
    // 对应 save()（manager.py:124-131）：UTF-8、缩进 4
    QDir().mkpath(m_configsDir);
    QFile file(filePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        cwn::Log::error(QStringLiteral("Save config failed: cannot open %1").arg(filePath()));
        return;
    }
    file.write(QJsonDocument(m_json).toJson(QJsonDocument::Indented));
    file.close();
    m_dirty = false;
    if (!silent)
        cwn::Log::info(QStringLiteral("Save config success: %1").arg(filePath()));
}

void ConfigStore::startAutoSave()
{
    m_saveTimer.start();
}

QVariant ConfigStore::data() const
{
    // manager.py:160-162 data 属性：整树转 dict/QVariant。
    // A1：命中缓存时零拷贝返回（QVariantMap 隐式共享），仅 m_json 变更后
    // 的首次读取重建一次
    if (!m_dataCacheValid) {
        m_dataCache = m_json.toVariantMap();
        m_dataCacheValid = true;
    }
    return m_dataCache;
}

bool ConfigStore::isKeyLocked(const QString &key) const
{
    // manager.py:154-158：精确全路径匹配（model.py:69 同）
    return m_lockedKeys.contains(key);
}

void ConfigStore::lock(const QStringList &keys)
{
    // manager.py:140-145 lock()：仅更新内存集合，不落盘
    for (const QString &k : keys)
        m_lockedKeys.insert(k);
    cwn::Log::info(QStringLiteral("Locked config keys: %1").arg(keys.join(QStringLiteral(", "))));
}

void ConfigStore::unlock(const QStringList &keys)
{
    // manager.py:147-152 unlock()
    for (const QString &k : keys)
        m_lockedKeys.remove(k);
    cwn::Log::info(
        QStringLiteral("Unlocked config keys: %1").arg(keys.join(QStringLiteral(", "))));
}

void ConfigStore::lock(const QString &key)
{
    lock(QStringList{ key });
}

void ConfigStore::unlock(const QString &key)
{
    unlock(QStringList{ key });
}

QStringList ConfigStore::lockedKeys() const
{
    // plugin/components.py:613-615 GlobalConfigAPI.locked_keys
    return QStringList(m_lockedKeys.cbegin(), m_lockedKeys.cend());
}

std::optional<QJsonValue> ConfigStore::value(const QString &dottedKey) const
{
    const QJsonValue v = jsonGetAt(m_json, dottedKey);
    if (v.isUndefined())
        return std::nullopt;
    return v;
}

void ConfigStore::setInternal(const QString &dottedKey, const QJsonValue &value)
{
    // 内部写入：不检查锁定（schedule/ 域回写已过锁定检查的路径）
    jsonSetAt(m_json, dottedKey, value);
    invalidateDataCache();
    emit dataChanged();
}

void ConfigStore::set(const QString &key, const QVariant &newValue)
{
    // 对应 manager.py:164-186 set()
    if (isKeyLocked(key)) {
        cwn::Log::warn(QStringLiteral("Attempt to modify locked config key: %1. Blocked.").arg(key));
        return;
    }

    QJsonValue jsonValue = QJsonValue::fromVariant(newValue);

    // model.py validate_assignment=True：叶子字段先校验/纠正；无法纠正时拒绝写入
    // （对应 Pydantic 抛 ValidationError，赋值不生效）
    if (const ScalarSpec *spec = findScalarSpec(key)) {
        const std::optional<QJsonValue> corrected = coerceScalar(*spec, jsonValue);
        if (!corrected.has_value()) {
            cwn::Log::warn(QStringLiteral("Config set rejected (type mismatch): %1").arg(key));
            return;
        }
        jsonValue = *corrected;
    }

    // 值未变化时不发信号（manager.py:178-186 的相等短路）
    // （参数名不能叫 value，否则会遮蔽成员函数 value(key)）
    if (std::optional<QJsonValue> current = this->value(key); current.has_value()) {
        if (*current == jsonValue)
            return;
    }

    setInternal(key, jsonValue);
}
