#pragma once

#include <QString>

namespace cwn::weather {

// 规范天气码：沿用小米 wtr-v3 代码集（weather.qml 的 weatherText() 消费同一
// 集合，各源映射到此集合后 QML 文本/图标逻辑无需按源分支）。
// kUnknownCode = 未知/未映射：QML 文本兜底"未知"，图标兜底 sun。
inline constexpr int kUnknownCode = -1;

int codeFromAmapText(const QString &text);       // 高德：天气现象中文文本 → 规范码
int codeFromQweatherCode(int code);              // 和风：100/300/400/500/900 系代码 → 规范码
int codeFromWeathercnIcon(int icon);             // 华风爱科：AccuWeather 图标号 1-47 → 规范码
int codeFromCaiyunSkycon(const QString &skycon); // 彩云：skycon 枚举字符串 → 规范码

// 规范码 → Fluent 图标（自 WeatherService.cpp weatherCodeToIcon 平移，行为不变）
QString iconForCode(int code, bool isNight);

// 预警等级 → 排序权重：B/Y/O/R（小米缩写）、中文颜色（和风/华风 level 原文）、
// 英文颜色码（华风 AlarmLevel/和风 color.code）等价归一；未知 → -1
int alertLevelRank(const QString &level);
// 预警等级原样值 → 规范字母 B/Y/O/R（未知归 B 最低档）
QString alertLevelLetter(const QString &raw);

} // namespace cwn::weather
