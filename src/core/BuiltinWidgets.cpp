#include "BuiltinWidgets.h"

#include "AppPaths.h"
#include "weather/WeatherService.h"

#include <QCoreApplication>
#include <QDate>
#include <QTime>
#include <QUrl>

WidgetBackend::WidgetBackend(QObject *parent)
    : QObject(parent)
{
}

QVariantMap WidgetBackend::getDateTime() const
{
    const QDate date = QDate::currentDate();
    const QTime time = QTime::currentTime();

    QVariantMap result;
    result.insert(QStringLiteral("hour"), QStringLiteral("%1").arg(time.hour(), 2, 10, QLatin1Char('0')));
    result.insert(QStringLiteral("minute"), QStringLiteral("%1").arg(time.minute(), 2, 10, QLatin1Char('0')));
    result.insert(QStringLiteral("second"), QStringLiteral("%1").arg(time.second(), 2, 10, QLatin1Char('0')));
    result.insert(QStringLiteral("year"), date.year());
    result.insert(QStringLiteral("month"), date.month());
    result.insert(QStringLiteral("day"), date.day());
    result.insert(QStringLiteral("weekday"), date.dayOfWeek()); // Qt: 1=周一 … 7=周日，与 isoweekday 一致
    return result;
}

QString WidgetBackend::sayHello() const
{
    return QStringLiteral("Hello from Class Widgets Next!");
}

QList<WidgetDefinition> BuiltinWidgetProvider::widgets() const
{
    // 来源 src/plugins/cw_widgets/widgets.py:24-79，widget_id / 名称 / qml 相对路径 /
    // settings_qml / default_settings 全部逐字对齐（存量配置契约）。
    const QString qmlRoot = AppPaths::instance().qmlRoot();
    const auto widgetUri = [&qmlRoot](const QString &relative) {
        return QUrl::fromLocalFile(qmlRoot + QLatin1Char('/') + relative);
    };

    WidgetDefinition currentActivity;
    currentActivity.id = QStringLiteral("classwidgets.currentActivity");
    currentActivity.name = WidgetBackend::tr("Current Activity");
    currentActivity.qmlPath = widgetUri(QStringLiteral("widgets/currentActivity.qml"));
    currentActivity.backendObj = nullptr; // 由注册方统一注入 backend 实例

    WidgetDefinition time;
    time.id = QStringLiteral("classwidgets.time");
    time.name = WidgetBackend::tr("Time");
    time.qmlPath = widgetUri(QStringLiteral("widgets/Time.qml"));

    WidgetDefinition eventCountdown;
    eventCountdown.id = QStringLiteral("classwidgets.eventCountdown");
    eventCountdown.name = WidgetBackend::tr("Event Countdown");
    eventCountdown.qmlPath = widgetUri(QStringLiteral("widgets/eventCountdown.qml"));

    // 即将上课默认显示缩写、默认最多 7 节；full_name=false 即"显示缩写"
    //（键名沿用上游存量契约，语义与 UI 相反）；marquee 已废弃，永不滚动，
    // 存量配置里残留的 marquee 键会被忽略，不做迁移清理。
    QVariantMap upcomingDefaults;
    upcomingDefaults.insert(QStringLiteral("max_activities"), 7);
    upcomingDefaults.insert(QStringLiteral("full_name"), false);

    WidgetDefinition upcomingActivities;
    upcomingActivities.id = QStringLiteral("classwidgets.upcomingActivities");
    upcomingActivities.name = WidgetBackend::tr("Upcoming Activities");
    upcomingActivities.qmlPath = widgetUri(QStringLiteral("widgets/upcomingActivities.qml"));
    upcomingActivities.settingsQml = widgetUri(QStringLiteral("widgets/settings/upcomingActivities.qml"));
    upcomingActivities.defaultSettings = upcomingDefaults;

    WidgetDefinition dynamicNotification;
    dynamicNotification.id = QStringLiteral("classwidgets.dynamicNotification");
    dynamicNotification.name = WidgetBackend::tr("Dynamic Notification");
    dynamicNotification.qmlPath = widgetUri(QStringLiteral("widgets/dynamicNotification.qml"));
    dynamicNotification.settingsQml = widgetUri(QStringLiteral("widgets/settings/upcomingActivities.qml"));
    dynamicNotification.defaultSettings = upcomingDefaults;

    QVariantMap textDefaults;
    textDefaults.insert(QStringLiteral("marquee"), false);
    textDefaults.insert(QStringLiteral("max_width"), 150);
    textDefaults.insert(QStringLiteral("text"), QString());

    WidgetDefinition customText;
    customText.id = QStringLiteral("classwidgets.customText");
    customText.name = WidgetBackend::tr("Text");
    customText.qmlPath = widgetUri(QStringLiteral("widgets/Text.qml"));
    customText.settingsQml = widgetUri(QStringLiteral("widgets/settings/Text.qml"));
    customText.defaultSettings = textDefaults;

    // 天气（本移植新增内置组件，上游 CW2 无对应注册项；多数据源见
    // weather-multi-provider-plan.md，扩展化见 extensions-feature-plan §5 B1）。
    // settingsQml 置空：右键「小组件设置」入口退役（WidgetsContainer 按
    // model.settingsQml 判空禁用该项），城市/数据源/凭据/刷新间隔全部收归
    // 全局「扩展功能-天气」页（pages/settings/Extensions/Weather.qml）。
    // maxInstances=1：城市已收敛为全局 weather.city，多实例只会是同一份数据的
    // 重复展示，开启扩展时自动添加也依赖该上限保持单一实例。
    // defaultSettings 的 city 键保留不删：存量实例配置形状不变（迁移同样保留
    // 实例上的旧键），组件本体已不再读取实例 settings.city，仅作无害残留。
    WidgetDefinition weather;
    weather.id = WeatherService::widgetTypeId();
    weather.name = WidgetBackend::tr("Weather");
    weather.qmlPath = widgetUri(QStringLiteral("widgets/weather.qml"));
    QVariantMap weatherDefaults;
    weatherDefaults.insert(QStringLiteral("city"), QString());
    weather.defaultSettings = weatherDefaults;
    weather.maxInstances = 1;

    // 倒数日（本移植新增内置组件，同天气先例为"新增文件"而非上游改动）；
    // settings.title 为事件名，settings.target_date 为 "yyyy-MM-dd" 目标日字符串，
    // settings.cycle 为重复周期 "none" | "weekly" | "monthly" | "yearly"（缺省 "none"，
    // 存量配置无此键时 loadPreset 以 defaultSettings 补齐，行为与旧版一致）。
    // 同一预设内最多 3 个实例（maxInstances 由 WidgetsModel.addInstance 强制执行，
    // 每个实例独立 settings，即最多同时显示 3 个不同的倒数日事件）。
    // 名称翻译走显式 "Widgets" 上下文（.ts 中内置组件名即归于此，同
    // ScheduleManager 的 QCoreApplication::translate 用法）
    WidgetDefinition countdownDays;
    countdownDays.id = QStringLiteral("classwidgets.countdownDays");
    countdownDays.name = QCoreApplication::translate("Widgets", "Days Countdown");
    countdownDays.qmlPath = widgetUri(QStringLiteral("widgets/countdownDays.qml"));
    countdownDays.settingsQml = widgetUri(QStringLiteral("widgets/settings/countdownDays.qml"));
    QVariantMap countdownDaysDefaults;
    countdownDaysDefaults.insert(QStringLiteral("title"), QString());
    countdownDaysDefaults.insert(QStringLiteral("target_date"), QString());
    countdownDaysDefaults.insert(QStringLiteral("cycle"), QStringLiteral("none"));
    countdownDays.defaultSettings = countdownDaysDefaults;
    countdownDays.maxInstances = 3;

    return { currentActivity, time, eventCountdown, upcomingActivities, dynamicNotification,
             customText, weather, countdownDays };
}
