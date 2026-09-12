#include "BuiltinWidgets.h"

#include "AppPaths.h"
#include "weather/WeatherService.h"

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

    QVariantMap upcomingDefaults;
    upcomingDefaults.insert(QStringLiteral("marquee"), true);
    upcomingDefaults.insert(QStringLiteral("max_activities"), 5);
    upcomingDefaults.insert(QStringLiteral("full_name"), true);

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

    // 天气（本移植新增内置组件，数据源小米天气 wtr-v3，上游 CW2 无对应注册项）；
    // settings.city 为城市 JSON 字符串，契约见 WeatherService 类注释
    WidgetDefinition weather;
    weather.id = WeatherService::widgetTypeId();
    weather.name = WidgetBackend::tr("Weather");
    weather.qmlPath = widgetUri(QStringLiteral("widgets/weather.qml"));
    weather.settingsQml = widgetUri(QStringLiteral("widgets/settings/weather.qml"));
    QVariantMap weatherDefaults;
    weatherDefaults.insert(QStringLiteral("city"), QString());
    weather.defaultSettings = weatherDefaults;

    return { currentActivity, time, eventCountdown, upcomingActivities, dynamicNotification,
             customText, weather };
}
