#include "WeatherCodes.h"

#include <QHash>

namespace cwn::weather {
namespace {

// 高德 weather 字段是天气现象中文文本（无数字代码），映射到规范码。
// 风力类文本（平静/微风/大风…）与"热/冷"无对应现象码 → kUnknownCode。
QHash<QString, int> amapTextTable()
{
    static const QHash<QString, int> table = {
        { QStringLiteral("晴"), 0 },
        { QStringLiteral("少云"), 1 },   { QStringLiteral("晴间多云"), 1 },
        { QStringLiteral("多云"), 1 },   { QStringLiteral("阴"), 2 },
        { QStringLiteral("阵雨"), 3 },   { QStringLiteral("强阵雨"), 3 },
        { QStringLiteral("雷阵雨"), 4 }, { QStringLiteral("强雷阵雨"), 4 },
        { QStringLiteral("雷阵雨并伴有冰雹"), 5 }, { QStringLiteral("雷阵雨伴有冰雹"), 5 },
        { QStringLiteral("雨夹雪"), 6 }, { QStringLiteral("雨雪天气"), 6 },
        { QStringLiteral("阵雨夹雪"), 6 },
        { QStringLiteral("小雨"), 7 },   { QStringLiteral("毛毛雨/细雨"), 7 },
        { QStringLiteral("中雨"), 8 },   { QStringLiteral("大雨"), 9 },
        { QStringLiteral("暴雨"), 10 },  { QStringLiteral("大暴雨"), 11 },
        { QStringLiteral("特大暴雨"), 12 }, { QStringLiteral("极端降雨"), 12 },
        { QStringLiteral("冻雨"), 19 },  { QStringLiteral("雨"), 301 },
        { QStringLiteral("小雨-中雨"), 21 }, { QStringLiteral("中雨-大雨"), 22 },
        { QStringLiteral("大雨-暴雨"), 23 }, { QStringLiteral("暴雨-大暴雨"), 24 },
        { QStringLiteral("大暴雨-特大暴雨"), 25 },
        { QStringLiteral("阵雪"), 13 },
        { QStringLiteral("小雪"), 14 },  { QStringLiteral("中雪"), 15 },
        { QStringLiteral("大雪"), 16 },  { QStringLiteral("暴雪"), 17 },
        { QStringLiteral("雪"), 302 },
        { QStringLiteral("小雪-中雪"), 26 }, { QStringLiteral("中雪-大雪"), 27 },
        { QStringLiteral("大雪-暴雪"), 28 },
        { QStringLiteral("雾"), 18 },    { QStringLiteral("轻雾"), 35 },
        { QStringLiteral("浓雾"), 32 },  { QStringLiteral("强浓雾"), 49 },
        { QStringLiteral("大雾"), 57 },  { QStringLiteral("特强浓雾"), 58 },
        { QStringLiteral("霾"), 53 },    { QStringLiteral("中度霾"), 54 },
        { QStringLiteral("重度霾"), 55 },{ QStringLiteral("严重霾"), 56 },
        { QStringLiteral("浮尘"), 20 },  { QStringLiteral("扬沙"), 20 },
        { QStringLiteral("沙尘暴"), 20 },{ QStringLiteral("强沙尘暴"), 20 },
    };
    return table;
}

QHash<QString, int> caiyunSkyconTable()
{
    static const QHash<QString, int> table = {
        { QStringLiteral("CLEAR_DAY"), 0 },          { QStringLiteral("CLEAR_NIGHT"), 0 },
        { QStringLiteral("PARTLY_CLOUDY_DAY"), 1 },  { QStringLiteral("PARTLY_CLOUDY_NIGHT"), 1 },
        { QStringLiteral("CLOUDY"), 2 },
        { QStringLiteral("LIGHT_HAZE"), 53 },        { QStringLiteral("MODERATE_HAZE"), 54 },
        { QStringLiteral("HEAVY_HAZE"), 55 },
        { QStringLiteral("LIGHT_RAIN"), 7 },         { QStringLiteral("MODERATE_RAIN"), 8 },
        { QStringLiteral("HEAVY_RAIN"), 9 },         { QStringLiteral("STORM_RAIN"), 10 },
        { QStringLiteral("FOG"), 18 },
        { QStringLiteral("LIGHT_SNOW"), 14 },        { QStringLiteral("MODERATE_SNOW"), 15 },
        { QStringLiteral("HEAVY_SNOW"), 16 },        { QStringLiteral("STORM_SNOW"), 17 },
        { QStringLiteral("DUST"), 20 },              { QStringLiteral("SAND"), 20 },
        // WIND（大风）无对应现象码 → kUnknownCode
    };
    return table;
}

} // namespace

int codeFromAmapText(const QString &text)
{
    return amapTextTable().value(text, kUnknownCode);
}

int codeFromQweatherCode(int code)
{
    switch (code) {
    case 100:
    case 150:
        return 0;
    case 101:
    case 102:
    case 103:
    case 151:
    case 152:
    case 153:
        return 1;
    case 104:
        return 2;
    case 300:
    case 301:
        return 3;
    case 302:
    case 303:
        return 4;
    case 304:
        return 5;
    case 305:
    case 309:
        return 7;
    case 306:
        return 8;
    case 307:
        return 9;
    case 308:
    case 312:
        return 12;
    case 310:
        return 10;
    case 311:
        return 11;
    case 313:
        return 19;
    case 314:
        return 21;
    case 315:
        return 22;
    case 316:
        return 23;
    case 317:
        return 24;
    case 318:
        return 25;
    case 399:
        return 301;
    case 400:
        return 14;
    case 401:
        return 15;
    case 402:
        return 16;
    case 403:
        return 17;
    case 404:
    case 405:
    case 406:
        return 6;
    case 407:
        return 13;
    case 408:
        return 26;
    case 409:
        return 27;
    case 410:
        return 28;
    case 499:
        return 302;
    case 500:
        return 35;
    case 501:
        return 18;
    case 502:
        return 53;
    case 503:
    case 504:
    case 507:
    case 508:
        return 20;
    case 509:
        return 32;
    case 510:
        return 49;
    case 511:
        return 54;
    case 512:
        return 55;
    case 513:
        return 56;
    case 514:
        return 57;
    case 515:
        return 58;
    default:
        // 和风官方声明代码表会增删，未知值必须兜底
        return kUnknownCode;
    }
}

int codeFromWeathercnIcon(int icon)
{
    // AccuWeather 图标号（1-47）：昼 1-32 / 夜 33-47。昼夜变体映射到同一规范码
    // （图标昼夜档由 iconForCode 按本机时钟解析）；冰雹/热/冷/大风等无对应码 →
    // kUnknownCode。
    if (icon <= 0)
        return kUnknownCode;
    switch (icon) {
    case 1:
    case 2:
    case 31:
    case 32:
    case 33:
    case 34:
        return 0;
    case 3:
    case 4:
    case 6:
    case 35:
    case 36:
        return 1;
    case 5:
        return 35; // Hazy Sunshine → 轻雾
    case 7:
    case 8:
        return 2;
    case 9:
        return 18;
    case 10:
    case 11:
    case 12:
    case 13:
    case 37:
    case 38:
        return 3; // Showers（含夜间变体）
    case 14:
    case 15:
    case 16:
    case 17:
    case 39:
    case 40:
        return 4; // Thunderstorms（含夜间变体）
    case 18:
        return 301;
    case 19:
    case 20:
    case 21:
    case 41:
    case 42:
    case 43:
    case 44:
        return 14; // Flurries（含夜间变体）
    case 22:
    case 23:
    case 45:
    case 46:
        return 15; // Snow
    case 24:
    case 25:
    case 26:
        return 19; // Ice/Sleet/Freezing Rain → 冻雨
    case 27:
        return 6; // Rain & Snow → 雨夹雪
    case 47:
        return 17;
    default:
        return kUnknownCode;
    }
}

int codeFromCaiyunSkycon(const QString &skycon)
{
    return caiyunSkyconTable().value(skycon, kUnknownCode);
}

QString iconForCode(int code, bool isNight)
{
    // 自 WeatherService.cpp weatherCodeToIcon 平移（0/1/3/13/26 分昼夜档，
    // 其余共用；未知兜底 sun）
    if (code == 0)
        return isNight ? QStringLiteral("ic_fluent_weather_moon_20_regular")
                       : QStringLiteral("ic_fluent_weather_sunny_20_regular");
    if (code == 1)
        return isNight ? QStringLiteral("ic_fluent_weather_partly_cloudy_night_20_regular")
                       : QStringLiteral("ic_fluent_weather_partly_cloudy_day_20_regular");
    if (code == 2)
        return QStringLiteral("ic_fluent_weather_cloudy_20_regular");
    if (code == 3)
        return isNight ? QStringLiteral("ic_fluent_weather_rain_showers_night_20_regular")
                       : QStringLiteral("ic_fluent_weather_rain_showers_day_20_regular");
    if (code == 4 || code == 5)
        return QStringLiteral("ic_fluent_weather_thunderstorm_20_regular");
    if (code == 6 || code == 19)
        return QStringLiteral("ic_fluent_weather_rain_snow_20_regular");
    if (code == 7 || code == 21 || code == 301)
        return QStringLiteral("ic_fluent_weather_drizzle_20_regular");
    if (code == 8 || code == 22 || (code >= 9 && code <= 12) || (code >= 23 && code <= 25))
        return QStringLiteral("ic_fluent_weather_rain_20_regular");
    if (code == 13 || code == 26)
        return isNight ? QStringLiteral("ic_fluent_weather_snow_shower_night_20_regular")
                       : QStringLiteral("ic_fluent_weather_snow_shower_day_20_regular");
    if ((code >= 14 && code <= 17) || (code >= 27 && code <= 28) || code == 302)
        return QStringLiteral("ic_fluent_weather_snow_20_regular");
    if (code == 18 || code == 32 || code == 35 || code == 49 || code == 57 || code == 58)
        return QStringLiteral("ic_fluent_weather_fog_20_regular");
    if (code >= 53 && code <= 56)
        return QStringLiteral("ic_fluent_weather_haze_20_regular");
    if (code == 20)
        return QStringLiteral("ic_fluent_weather_duststorm_20_regular");
    return QStringLiteral("ic_fluent_weather_sunny_20_regular");
}

int alertLevelRank(const QString &level)
{
    const QString trimmed = level.trimmed();
    if (trimmed.isEmpty())
        return -1;
    if (trimmed == QLatin1String("B") || trimmed.contains(QLatin1Char('蓝'))
        || trimmed.compare(QLatin1String("Blue"), Qt::CaseInsensitive) == 0)
        return 0;
    if (trimmed == QLatin1String("Y") || trimmed.contains(QLatin1Char('黄'))
        || trimmed.compare(QLatin1String("Yellow"), Qt::CaseInsensitive) == 0)
        return 1;
    if (trimmed == QLatin1String("O") || trimmed.contains(QLatin1Char('橙'))
        || trimmed.compare(QLatin1String("Orange"), Qt::CaseInsensitive) == 0
        || trimmed.compare(QLatin1String("Amber"), Qt::CaseInsensitive) == 0)
        return 2;
    if (trimmed == QLatin1String("R") || trimmed.contains(QLatin1Char('红'))
        || trimmed.compare(QLatin1String("Red"), Qt::CaseInsensitive) == 0)
        return 3;
    return -1;
}

QString alertLevelLetter(const QString &raw)
{
    switch (alertLevelRank(raw)) {
    case 3:
        return QStringLiteral("R");
    case 2:
        return QStringLiteral("O");
    case 1:
        return QStringLiteral("Y");
    default:
        return QStringLiteral("B");
    }
}

} // namespace cwn::weather
