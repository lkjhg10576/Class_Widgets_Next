#include "ConfigStore.h"

#include "Logger.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLocale>
#include <QVariant>

#include <optional>

namespace {
// 对应上游 src/__init__.py 的 __version__ / __version_type__
// （保持与上游一致，避免存量 configs.json 触发版本不匹配分支）
const char kAppVersion[] = "2.0.0.dev31546377";
const char kAppChannel[] = "alpha";
const char kDefaultFont[] = "Microsoft YaHei";

// 对应 model.py 中 PreferencesConfig.widgets_presets 的默认 preset
QJsonObject defaultWidgetPreset(const char *typeId, const char *instanceId)
{
    QJsonObject entry;
    entry.insert(QStringLiteral("type_id"), QString::fromLatin1(typeId));
    entry.insert(QStringLiteral("instance_id"), QString::fromLatin1(instanceId));
    entry.insert(QStringLiteral("settings"), QJsonObject());
    return entry;
}
} // namespace

ConfigStore::ConfigStore(QString configsDir, QObject *parent)
    : QObject(parent)
    , m_configsDir(std::move(configsDir))
{
    m_saveTimer.setInterval(60 * 1000); // manager.py: save_timer 1 分钟
    connect(&m_saveTimer, &QTimer::timeout, this, [this] {
        if (m_dirty)
            save();
    });

    connect(this, &ConfigStore::dataChanged, this, [this] { m_dirty = true; });
}

QString ConfigStore::filePath() const
{
    return m_configsDir + QStringLiteral("/configs.json");
}

QJsonObject ConfigStore::defaultConfig()
{
    QJsonObject app;
    app.insert(QStringLiteral("debug_mode"), false);
    app.insert(QStringLiteral("no_logs"), false);
    app.insert(QStringLiteral("version"), QString::fromLatin1(kAppVersion));
    app.insert(QStringLiteral("channel"), QString::fromLatin1(kAppChannel));
    app.insert(QStringLiteral("tutorial_completed"), false);
    app.insert(QStringLiteral("auto_startup"), false);

    QJsonObject locale;
    locale.insert(QStringLiteral("language"), QLocale::system().name());

    QJsonObject defaultDuration;
    // Pydantic 字段名 class_/break_ 序列化后保留原字段名（存量配置契约）
    defaultDuration.insert(QStringLiteral("class_"), 40);
    defaultDuration.insert(QStringLiteral("break_"), 10);
    defaultDuration.insert(QStringLiteral("activity"), 30);

    QJsonObject schedule;
    schedule.insert(QStringLiteral("current_schedule"), QStringLiteral("New Schedule 1"));
    schedule.insert(QStringLiteral("preparation_time"), 2);
    schedule.insert(QStringLiteral("default_duration"), defaultDuration);
    schedule.insert(QStringLiteral("time_offset"), 0);
    schedule.insert(QStringLiteral("reschedule_day"), QJsonObject());
    schedule.insert(QStringLiteral("class_swap"), QJsonObject());

    QJsonArray shortcuts;
    shortcuts.append(QStringLiteral("com.classwidgets.settings"));
    shortcuts.append(QStringLiteral("com.classwidgets.schedules"));
    shortcuts.append(QStringLiteral("com.classwidgets.plugin-plaza"));
    shortcuts.append(QStringLiteral("com.classwidgets.reschedule-day"));
    shortcuts.append(QStringLiteral("com.classwidgets.class-swap"));

    QJsonArray defaultPreset;
    defaultPreset.append(defaultWidgetPreset("classwidgets.time",
                                             "8ee721ef-ab36-4c23-834d-2c666a6739a3"));
    defaultPreset.append(defaultWidgetPreset("classwidgets.dynamicNotification",
                                             "4ccfdd24-eac1-4be0-8a09-7271af818327"));
    defaultPreset.append(defaultWidgetPreset("classwidgets.currentActivity",
                                             "87985398-2844-4c9e-b27d-6ea81cd0a2c6"));
    QJsonObject widgetsPresets;
    widgetsPresets.insert(QStringLiteral("default"), defaultPreset);

    QJsonObject preferences;
    preferences.insert(QStringLiteral("current_theme"), QStringLiteral("com.classwidgets.default"));
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

    QJsonObject hide;
    hide.insert(QStringLiteral("state"), false);
    hide.insert(QStringLiteral("in_class"), false);
    hide.insert(QStringLiteral("clicked"), true);
    hide.insert(QStringLiteral("maximized"), false);
    hide.insert(QStringLiteral("fullscreen"), false);
    hide.insert(QStringLiteral("action"), QStringLiteral("hide"));

    QJsonObject interactions;
    interactions.insert(QStringLiteral("hover_fade"), false);
    interactions.insert(QStringLiteral("hide"), hide);
    interactions.insert(QStringLiteral("tapped_action"), QStringLiteral("hide"));

    QJsonObject plugins;
    QJsonArray enabledPlugins;
    enabledPlugins.append(QStringLiteral("builtin.classwidgets.widgets"));
    plugins.insert(QStringLiteral("enabled"), enabledPlugins);
    plugins.insert(QStringLiteral("configs"), QJsonObject());
    plugins.insert(QStringLiteral("pending_operations"), QJsonArray());
    plugins.insert(QStringLiteral("auto_check_plaza_updates"), true);
    plugins.insert(QStringLiteral("auto_install_plaza_updates"), false);

    QJsonObject mirrors;
    mirrors.insert(QStringLiteral("gh_proxy"), QStringLiteral("https://gh-proxy.com/"));
    mirrors.insert(QStringLiteral("kkgithub"), QStringLiteral("https://kkgithub.com/"));
    mirrors.insert(QStringLiteral("gitfast"), QStringLiteral("https://gitfast.top/"));

    QJsonObject network;
    network.insert(QStringLiteral("mirrors"), mirrors);
    network.insert(QStringLiteral("current_mirror"), QStringLiteral("gh_proxy"));
    network.insert(QStringLiteral("mirror_enabled"), true);
    network.insert(QStringLiteral("releases_url"),
                   QStringLiteral("https://classwidgets.rinlit.cn/2/releases.json"));
    network.insert(QStringLiteral("auto_check_updates"), true);
    network.insert(QStringLiteral("plaza_url"), QStringLiteral("https://plaza.cw.rinlit.cn"));

    QJsonObject levelSounds;
    levelSounds.insert(QStringLiteral("0"), QString());
    levelSounds.insert(QStringLiteral("1"), QString());
    levelSounds.insert(QStringLiteral("2"), QString());
    levelSounds.insert(QStringLiteral("3"), QString());

    QJsonObject notifications;
    notifications.insert(QStringLiteral("enabled"), true);
    notifications.insert(QStringLiteral("default_sound"), QJsonValue::Null);
    notifications.insert(QStringLiteral("volume"), 0.7);
    notifications.insert(QStringLiteral("providers"), QJsonObject());
    notifications.insert(QStringLiteral("default_duration"), 8000);
    notifications.insert(QStringLiteral("level_sounds"), levelSounds);

    QJsonObject root;
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
    // 已加载的键优先，缺失的键用默认值补齐（近似 Pydantic 的字段缺省行为）
    QJsonObject merged = loaded;
    for (auto it = defaults.begin(); it != defaults.end(); ++it) {
        if (!merged.contains(it.key()))
            merged.insert(it.key(), it.value());
        else if (it.value().isObject() && merged.value(it.key()).isObject())
            merged.insert(it.key(), mergeDefaults(it.value().toObject(),
                                                  merged.value(it.key()).toObject()));
    }
    return merged;
}

void ConfigStore::ensureDefaults()
{
    // 对应 _ensure_defaults()：版本号对齐 + Windows 字体回退
    QJsonObject app = m_json.value(QStringLiteral("app")).toObject();
    if (app.value(QStringLiteral("version")).toString() != QString::fromLatin1(kAppVersion)
        || app.value(QStringLiteral("channel")).toString() != QString::fromLatin1(kAppChannel)) {
        cwn::Log::warn(QStringLiteral(
                            "Config version mismatch: %1 %2 != %3 %4, rewriting")
                            .arg(app.value(QStringLiteral("version")).toString(),
                                 app.value(QStringLiteral("channel")).toString(),
                                 QString::fromLatin1(kAppVersion), QString::fromLatin1(kAppChannel)));
        app.insert(QStringLiteral("version"), QString::fromLatin1(kAppVersion));
        app.insert(QStringLiteral("channel"), QString::fromLatin1(kAppChannel));
        m_json.insert(QStringLiteral("app"), app);
    }

    QJsonObject preferences = m_json.value(QStringLiteral("preferences")).toObject();
    if (preferences.value(QStringLiteral("font")).toString().isEmpty()) {
        preferences.insert(QStringLiteral("font"), QString::fromLatin1(kDefaultFont));
        m_json.insert(QStringLiteral("preferences"), preferences);
    }
}

void ConfigStore::load()
{
    QFile file(filePath());
    if (file.exists()) {
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QJsonParseError parseError;
            const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
            file.close();
            if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
                m_json = mergeDefaults(defaultConfig(), doc.object());
                ensureDefaults();
                cwn::Log::info(QStringLiteral("Loaded config: %1").arg(filePath()));
            } else {
                cwn::Log::warn(QStringLiteral("Load config failed: %1, use default config")
                                   .arg(parseError.errorString()));
                m_json = defaultConfig();
            }
        }
    } else {
        m_json = defaultConfig();
        cwn::Log::info(QStringLiteral("No config found, created default config at %1")
                           .arg(filePath()));
    }
    save(true);
}

void ConfigStore::save(bool silent)
{
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
    return m_json.toVariantMap();
}

bool ConfigStore::isKeyLocked(const QString &key) const
{
    return m_lockedKeys.contains(key);
}

void ConfigStore::lock(const QStringList &keys)
{
    for (const QString &k : keys)
        m_lockedKeys.insert(k);
    cwn::Log::info(QStringLiteral("Locked config keys: %1").arg(keys.join(QStringLiteral(", "))));
}

void ConfigStore::unlock(const QStringList &keys)
{
    for (const QString &k : keys)
        m_lockedKeys.remove(k);
    cwn::Log::info(QStringLiteral("Unlocked config keys: %1").arg(keys.join(QStringLiteral(", "))));
}

std::optional<QJsonValue> ConfigStore::value(const QString &dottedKey) const
{
    const QStringList parts = dottedKey.split(QLatin1Char('.'));
    if (parts.isEmpty())
        return std::nullopt;

    QJsonValue current(m_json);
    for (const QString &part : parts) {
        if (!current.isObject())
            return std::nullopt;
        current = current.toObject().value(part);
        if (current.isUndefined())
            return std::nullopt;
    }
    return current;
}

void ConfigStore::setInternal(const QString &dottedKey, const QJsonValue &value)
{
    const QStringList parts = dottedKey.split(QLatin1Char('.'));
    if (parts.isEmpty())
        return;

    // QJsonObject 无引用语义：逐层取对象副本，自底向上写入后整体回写根对象
    QList<QJsonObject> levels;
    levels.append(m_json);
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

    m_json = newValue.toObject();
    emit dataChanged();
}

void ConfigStore::set(const QString &key, const QVariant &value)
{
    if (isKeyLocked(key)) {
        cwn::Log::warn(QStringLiteral("Attempt to modify locked config key: %1. Blocked.").arg(key));
        return;
    }

    const QJsonValue jsonValue = QJsonValue::fromVariant(value);
    // 值未变化时不发信号（对应 manager.py set 的相等短路）
    if (std::optional<QJsonValue> current = value(key); current.has_value()) {
        if (*current == jsonValue)
            return;
    }

    setInternal(key, jsonValue);
}
