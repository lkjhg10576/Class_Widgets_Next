#pragma once

#include "../WeatherProvider.h"

#include <QHash>

class ConfigStore;

// NMC（中央气象台，免 Key）数据源（four-plugins §5.1 P3）。
// 对应上游 `com.weather` v1.2.1（`main.py` 702 行 + qml + `cities.js` 62KB）的 NMC 分支，
// Next 落点为第 6 数据源 `NmcProvider`（免 Key；双源融合降级为单源 + 备源回退，不抢默认源）。
//
// 协议（UA 伪装 Chrome）：
//   城市索引：`GET /province/all` + 并行 `GET /province/{code}` 建 `[{city,code,province}]`，
//     会话缓存 + 落盘 `configs/weather/nmc_station_index.json`（AppPaths::configsRoot 解析，
//     带 version 字段；质检修正：原 QDir::current 相对路径在打包运行时不可控，且
//     "插件目录旧文件迁移"指向仓库中不存在的 example/ 目录属死代码，已移除）；
//     匹配 精确 > 去后缀（11 种）> 包含 > 省名回退取省内首城（同名县市带省信息时
//     优先取同省命中）。
//   实况+预报：`GET /weather?stationid={code}`（响应根为 {"data":{...}} 包装，实况字段
//     在 real.weather / real.wind 嵌套内，今日 hi/lo 在 predict.detail[0].day|night
//     .weather.temperature——形状对照上游 main.py 实测源码），数值宽容字符串（质检
//     修正：NMC 字段多以字符串返回），哨兵 9999/999 过滤，温度 ±60°C、湿度 0-100
//     校验，体感差 >20 疑似华氏则换算（换算后复验 ±20 内），夜间最高温 9999 用实况
//     兜底 + 当日最高温 day-cache（跨天条目清理），强制 `hi>=lo`。
//   预警：`GET /findAlarm?pageNo=1..3&pageSize=50&province={省}`（响应 data.page.list，
//     条目仅 title/issuetime，等级自标题文字提取——上游 _alert_level），`city in title`
//     过滤 + 他省省级名交叉剔除，红 0 < 橙 1 < 黄 2 < 蓝 3 < 白 4 排序取前 3 →
//     门面只推最高 1 条；NMC 无 alertId，合成 `hash(title+issuetime)` 供会话去重；
//     中文 info → 本文件私有映射表（确定性最长匹配）；`wind.direct+power → windScale`
//     文本，`windSpeed` 留空。
//   双源融合降级：`weather.com.cn` 只作内部备源回退（toy1/search 取码 + d1 文本页
//     解析 var dataSK/cityDZ，补描述/湿度/最低温等缺口，只填不改 → "温差 >8° 仍用
//     NMC" 天然满足），不拆第 7 源，不改门面降级逻辑；备源失败保留 NMC 部分与
//     预警原样返回（质检修正：原实现丢弃 alerts 且必报 parse）。
class NmcProvider final : public WeatherProvider
{
    Q_OBJECT
public:
    explicit NmcProvider(ConfigStore *configs, QNetworkAccessManager *nam, QObject *parent = nullptr);

    QString id() const override { return QStringLiteral("nmc"); }
    bool isConfigured() const override { return true; } // 免 Key
    void searchCity(const QString &keyword) override;
    void fetch(const CityInfo &city) override;
    void testConnection() override;

    // CityInfo.nmcCode 回填：QML 三级城市选后回写（`weather.city` JSON 的 nmcCode 键）。
    static QString codeForCity(const CityInfo &city);

private:
    struct StationEntry
    {
        QString city;
        QString code;
        QString province;
    };

    void ensureStationIndex(const std::function<void(bool ok)> &done);
    QString indexFilePath() const;
    void loadIndexFromDisk();
    void saveIndexToDisk() const;
    QList<StationEntry> matchStations(const QString &keyword) const;
    QString resolveStationCode(const CityInfo &city) const;
    void fetchWithStation(const CityInfo &city, const QString &stationCode);
    // 备源回退（详见类注释）：完成时发 fetchFinished；alerts 透传不丢弃
    void fetchWeatherComCnFallback(const CityInfo &city, const QVariantMap &partialCurrent,
                                   const QVariantMap &partialToday, const QVariantList &alerts);
    void finishFallback(const CityInfo &city, const QVariantMap &current,
                        const QVariantMap &today, const QVariantList &alerts);
    // 备源文本页 GET（d1 响应为 `var x = {...};` 非 JSON，getJson 不适用）
    void getText(const QUrl &url,
                 const std::function<void(const QString &body, const QString &errorKind)> &done);

    ConfigStore *m_configs = nullptr;
    QList<StationEntry> m_stations; // 会话缓存
    bool m_indexReady = false;
    bool m_indexLoading = false;
    QList<std::function<void(bool ok)>> m_indexWaiters;
    QHash<QString, double> m_dayMaxCache; // 当日最高温 day-cache：key=stationCode+yyyy-MM-dd
};
