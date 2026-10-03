#include "NmcProvider.h"

#include "../../AppPaths.h"
#include "../../ConfigStore.h"
#include "../../Logger.h"
#include "../WeatherCodes.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <functional>

namespace {
constexpr auto kNmcBase = "http://www.nmc.cn/rest";
constexpr auto kNmcUa = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                        "(KHTML, like Gecko) Chrome/120.0 Safari/537.36";
// 11 种行政区后缀（匹配优先级：精确 > 去后缀 > 包含 > 省名回退）
const QStringList kSuffixes = {
    QStringLiteral("特别行政区"), QStringLiteral("自治区"), QStringLiteral("自治州"),
    QStringLiteral("自治县"), QStringLiteral("自治旗"), QStringLiteral("地区"),
    QStringLiteral("盟"), QStringLiteral("市"), QStringLiteral("区"),
    QStringLiteral("县"), QStringLiteral("省"),
};
// 全国省级名主干（去后缀）：预警标题他省交叉剔除用（质检修正：province 缺失/
// 服务端忽略参数时，"朝阳"可命中他省"××市朝阳区"标题）
const QStringList kProvinceNames = {
    QStringLiteral("北京"), QStringLiteral("天津"), QStringLiteral("河北"),
    QStringLiteral("山西"), QStringLiteral("内蒙古"), QStringLiteral("辽宁"),
    QStringLiteral("吉林"), QStringLiteral("黑龙江"), QStringLiteral("上海"),
    QStringLiteral("江苏"), QStringLiteral("浙江"), QStringLiteral("安徽"),
    QStringLiteral("福建"), QStringLiteral("江西"), QStringLiteral("山东"),
    QStringLiteral("河南"), QStringLiteral("湖北"), QStringLiteral("湖南"),
    QStringLiteral("广东"), QStringLiteral("广西"), QStringLiteral("海南"),
    QStringLiteral("重庆"), QStringLiteral("四川"), QStringLiteral("贵州"),
    QStringLiteral("云南"), QStringLiteral("西藏"), QStringLiteral("陕西"),
    QStringLiteral("甘肃"), QStringLiteral("青海"), QStringLiteral("宁夏"),
    QStringLiteral("新疆"), QStringLiteral("香港"), QStringLiteral("澳门"),
    QStringLiteral("台湾"),
};
// weather.com.cn 备源请求头（对照上游 main.py:24-32 CN_HEADERS）：无 Referer
// 时 d1/toy1 返回 4xx，缺一不可
std::list<std::pair<QString, QString>> cnHeaders()
{
    return { { QStringLiteral("User-Agent"), QString::fromLatin1(kNmcUa) },
             { QStringLiteral("Referer"), QStringLiteral("http://www.weather.com.cn/") } };
}
constexpr auto kIndexFileTail = "/weather/nmc_station_index.json";
// 索引文件版本：结构变化时整体重建（旧版无 version 字段 → 版本不符走在线重建）
constexpr int kIndexVersion = 2;

QString stripSuffix(const QString &name)
{
    for (const QString &s : kSuffixes) {
        if (name.endsWith(s) && name.size() > s.size())
            return name.left(name.size() - s.size());
    }
    return name;
}

bool isSentinel(double v)
{
    return qFuzzyCompare(v, 9999.0) || qFuzzyCompare(v, 999.0) || v >= 999.0;
}

// 数值宽容读取（NMC /rest/weather 的字段多以字符串返回）：非数值 → fallback
double numOf(const QJsonObject &obj, const char *key, double fallback)
{
    const QJsonValue v = obj.value(QLatin1String(key));
    if (v.isDouble())
        return v.toDouble();
    if (v.isString()) {
        bool ok = false;
        const double d = v.toString().toDouble(&ok);
        if (ok)
            return d;
    }
    return fallback;
}

// 快照是否含实义字段（有效性判定基准）：precipProb 占位不计入——补充质检修正，
// 备源路径原以 map 非空判 valid，{precipProb:0} 也算"成功"，零数据快照伪装 ok
// 并抑制重试约 0.9×轮询间隔
bool snapshotHasData(const QVariantMap &current, const QVariantMap &today)
{
    return current.contains(QLatin1String("temperature"))
        || current.contains(QLatin1String("feelsLike"))
        || current.contains(QLatin1String("humidity"))
        || current.contains(QLatin1String("windScale"))
        || current.contains(QLatin1String("weatherCode"))
        || today.contains(QLatin1String("tempMax"))
        || today.contains(QLatin1String("tempMin"))
        || today.contains(QLatin1String("dayCode"))
        || today.contains(QLatin1String("nightCode"));
}

// NMC 中文天气 info → 规范码。映射为 NMC 私有表（与 amapTextTable 语义相近，
// WeatherCodes 主表不含中文源文本键）。有序表按 key 长度降序，最长匹配优先，
// "转"只取首段定性（QHash 迭代序随机化已弃用）。
int codeFromNmcInfo(const QString &info)
{
    struct Entry
    {
        QString key;
        int code;
    };
    static const QList<Entry> table = [] {
        const QList<QPair<QString, int>> raw = {
            { QStringLiteral("晴"), 0 },
            { QStringLiteral("多云"), 1 }, { QStringLiteral("少云"), 1 }, { QStringLiteral("阴"), 2 },
            { QStringLiteral("阵雨"), 3 }, { QStringLiteral("雷阵雨"), 4 }, { QStringLiteral("冰雹"), 5 },
            { QStringLiteral("雨夹雪"), 6 }, { QStringLiteral("小雨"), 7 }, { QStringLiteral("中雨"), 8 },
            { QStringLiteral("大雨"), 9 }, { QStringLiteral("暴雨"), 10 }, { QStringLiteral("大暴雨"), 11 },
            { QStringLiteral("特大暴雨"), 12 }, { QStringLiteral("阵雪"), 13 }, { QStringLiteral("小雪"), 14 },
            { QStringLiteral("中雪"), 15 }, { QStringLiteral("大雪"), 16 }, { QStringLiteral("暴雪"), 17 },
            { QStringLiteral("雾"), 18 }, { QStringLiteral("冻雨"), 19 }, { QStringLiteral("沙尘暴"), 20 },
            { QStringLiteral("浮尘"), 20 }, { QStringLiteral("扬沙"), 20 }, { QStringLiteral("霾"), 53 },
            { QStringLiteral("雨"), 301 }, { QStringLiteral("雪"), 302 },
        };
        QList<Entry> t;
        for (const auto &p : raw)
            t.append({ p.first, p.second });
        std::sort(t.begin(), t.end(), [](const Entry &a, const Entry &b) {
            return a.key.size() > b.key.size();
        });
        return t;
    }();
    QString head = info.trimmed();
    const int turnAt = head.indexOf(QLatin1Char('转'));
    if (turnAt > 0)
        head = head.left(turnAt); // "小雨转多云"按首段定性
    if (head.isEmpty())
        return cwn::weather::kUnknownCode;
    for (const Entry &e : table) {
        if (head == e.key)
            return e.code;
    }
    for (const Entry &e : table) { // 最长 key 优先的前后缀兜底（确定性）
        if (head.startsWith(e.key) || head.endsWith(e.key))
            return e.code;
    }
    return cwn::weather::kUnknownCode;
}

std::list<std::pair<QString, QString>> nmcHeaders()
{
    return { { QStringLiteral("User-Agent"), QString::fromLatin1(kNmcUa) },
             { QStringLiteral("Referer"), QStringLiteral("http://www.nmc.cn/") } };
}
} // namespace

NmcProvider::NmcProvider(ConfigStore *configs, QNetworkAccessManager *nam, QObject *parent)
    : WeatherProvider(nam, parent)
    , m_configs(configs)
{
    loadIndexFromDisk();
}

QString NmcProvider::codeForCity(const CityInfo &city)
{
    return city.nmcCode;
}

void NmcProvider::searchCity(const QString &keyword)
{
    const QString trimmed = keyword.trimmed();
    if (trimmed.isEmpty()) {
        emit citySearchFinished({});
        return;
    }
    ensureStationIndex([this, trimmed](bool ok) {
        QVariantList out;
        if (ok) {
            for (const StationEntry &e : matchStations(trimmed)) {
                QVariantMap city;
                city.insert(QStringLiteral("cityId"), e.code);
                city.insert(QStringLiteral("name"), e.city);
                city.insert(QStringLiteral("lat"), 0.0);
                city.insert(QStringLiteral("lon"), 0.0);
                city.insert(QStringLiteral("province"), e.province);
                city.insert(QStringLiteral("nmcCode"), e.code);
                out.append(city);
                if (out.size() >= 20)
                    break;
            }
        }
        emit citySearchFinished(out);
    });
}

void NmcProvider::fetch(const CityInfo &city)
{
    const QString code = resolveStationCode(city);
    if (code.isEmpty()) {
        // 无站号：先建索引再重试一次（首建约 30 请求，后台 + 进度由 QML busy 体现）
        ensureStationIndex([this, city](bool ok) {
            if (!ok) {
                emit fetchFinished(city.cityId, {}, QStringLiteral("network"));
                return;
            }
            const QString retry = resolveStationCode(city);
            if (retry.isEmpty())
                emit fetchFinished(city.cityId, {}, QStringLiteral("parse"));
            else
                fetchWithStation(city, retry);
        });
        return;
    }
    fetchWithStation(city, code);
}

void NmcProvider::testConnection()
{
    // 免 Key 最小请求：GET /province/all
    QUrl url(QString::fromLatin1(kNmcBase) + QStringLiteral("/province/all"));
    getJson(url, nmcHeaders(), cwn::weather::kFetchTimeoutMs,
            [this](const QJsonDocument &doc, const QString &errorKind, int) {
                if (!errorKind.isEmpty()) {
                    emit connectionTestFinished(false, errorKind);
                    return;
                }
                const bool ok = doc.isArray() && !doc.array().isEmpty();
                emit connectionTestFinished(ok, ok ? QString() : QStringLiteral("parse"));
            });
}

// ── 站号索引 ──

void NmcProvider::ensureStationIndex(const std::function<void(bool ok)> &done)
{
    if (m_indexReady && !m_stations.isEmpty()) {
        done(true);
        return;
    }
    m_indexWaiters.append(done);
    if (m_indexLoading)
        return;
    m_indexLoading = true;
    QUrl url(QString::fromLatin1(kNmcBase) + QStringLiteral("/province/all"));
    getJson(url, nmcHeaders(), cwn::weather::kFetchTimeoutMs,
            [this](const QJsonDocument &doc, const QString &errorKind, int) {
                if (!errorKind.isEmpty() || !doc.isArray()) {
                    m_indexLoading = false;
                    for (auto &w : m_indexWaiters)
                        w(false);
                    m_indexWaiters.clear();
                    return;
                }
                // 并行拉取各省站点（每省一请求，约 30 个；QNAM 同主机并发由
                // HTTP/1.1 连接数自然限流，逐个回包聚合）
                const QJsonArray provinces = doc.array();
                if (provinces.isEmpty()) {
                    m_indexLoading = false;
                    for (auto &w : m_indexWaiters)
                        w(false);
                    m_indexWaiters.clear();
                    return;
                }
                auto pending = std::make_shared<int>(provinces.size());
                auto allStations = std::make_shared<QList<StationEntry>>();
                for (const QJsonValue &p : provinces) {
                    const QJsonObject obj = p.toObject();
                    const QString pCode = obj.value(QLatin1String("code")).toString();
                    const QString pName = obj.value(QLatin1String("name")).toString();
                    if (pCode.isEmpty()) {
                        if (--(*pending) == 0) {
                            m_stations = *allStations;
                            m_indexReady = !m_stations.isEmpty();
                            m_indexLoading = false;
                            saveIndexToDisk();
                            for (auto &w : m_indexWaiters)
                                w(m_indexReady);
                            m_indexWaiters.clear();
                        }
                        continue;
                    }
                    QUrl curl(QString::fromLatin1(kNmcBase) + QStringLiteral("/province/") + pCode);
                    getJson(curl, nmcHeaders(), cwn::weather::kFetchTimeoutMs,
                            [this, pName, pending, allStations](const QJsonDocument &cdoc,
                                                               const QString &cerr, int) {
                                if (cerr.isEmpty() && cdoc.isArray()) {
                                    for (const QJsonValue &s : cdoc.array()) {
                                        const QJsonObject so = s.toObject();
                                        StationEntry e;
                                        e.city = so.value(QLatin1String("city")).toString();
                                        e.code = so.value(QLatin1String("code")).toString();
                                        e.province = pName;
                                        if (!e.city.isEmpty() && !e.code.isEmpty())
                                            allStations->append(e);
                                    }
                                }
                                if (--(*pending) == 0) {
                                    m_stations = *allStations;
                                    m_indexReady = !m_stations.isEmpty();
                                    m_indexLoading = false;
                                    saveIndexToDisk();
                                    for (auto &w : m_indexWaiters)
                                        w(m_indexReady);
                                    m_indexWaiters.clear();
                                }
                            });
                }
            });
}

QList<NmcProvider::StationEntry> NmcProvider::matchStations(const QString &keyword) const
{
    QList<StationEntry> exact, stripped, contains;
    const QString keyStripped = stripSuffix(keyword);
    for (const StationEntry &e : m_stations) {
        if (e.city == keyword) {
            exact.append(e);
            continue;
        }
        if (stripSuffix(e.city) == keyStripped || stripSuffix(e.city) == keyword
            || e.city == keyStripped) {
            stripped.append(e);
            continue;
        }
        if (e.city.contains(keyword) || keyword.contains(e.city))
            contains.append(e);
    }
    QList<StationEntry> out = exact + stripped + contains;
    // 省名回退：无命中时取省内首城
    if (out.isEmpty()) {
        for (const StationEntry &e : m_stations) {
            if (e.province.contains(keyword) || keyword.contains(e.province)) {
                out.append(e);
                break;
            }
        }
    }
    return out;
}

QString NmcProvider::resolveStationCode(const CityInfo &city) const
{
    // 优先级：显式 nmcCode（字母码）> cityId 本身是字母码 > 索引匹配 > 省回退
    auto isLetterCode = [](const QString &s) {
        if (s.isEmpty())
            return false;
        for (QChar c : s) {
            if (c.isDigit())
                return false;
        }
        return true;
    };
    // 纯数字强制重建：数字码不是 NMC 站号（字母码，2026 接口改版后数字码一律
    // 返回空数据）。nmcCode 同样拒绝——质检修正：设置页旧版把非 NMC 源的数字
    // cityId 污染进 nmcCode，无条件采信会让 NMC 源永远请求错误站号
    if (isLetterCode(city.nmcCode))
        return city.nmcCode;
    if (isLetterCode(city.cityId))
        return city.cityId;
    const QList<StationEntry> hits = matchStations(city.name);
    // 同名县市歧义（朝阳区：北京/长春）：城市带省信息时优先取同省命中。
    // 省名两侧各去后缀再比较（补充质检修正：非 NMC 源写入的省名可能带
    // "省/市"全称，与索引内的主干名精确等值会失配）
    if (!city.province.isEmpty()) {
        const QString cityProv = stripSuffix(city.province);
        for (const StationEntry &e : hits) {
            if (stripSuffix(e.province) == cityProv)
                return e.code;
        }
    }
    if (!hits.isEmpty())
        return hits.first().code;
    // 省名回退取省内首城
    if (!city.province.isEmpty()) {
        const QString cityProv = stripSuffix(city.province);
        for (const StationEntry &e : m_stations) {
            if (stripSuffix(e.province) == cityProv)
                return e.code;
        }
    }
    return {};
}

void NmcProvider::fetchWithStation(const CityInfo &city, const QString &stationCode)
{
    QUrl url(QString::fromLatin1(kNmcBase) + QStringLiteral("/weather"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("stationid"), stationCode);
    url.setQuery(q);
    getJson(url, nmcHeaders(), cwn::weather::kFetchTimeoutMs,
            [this, city, stationCode](const QJsonDocument &doc, const QString &errorKind, int) {
                if (!errorKind.isEmpty() || !doc.isObject()) {
                    emit fetchFinished(city.cityId, {}, errorKind.isEmpty() ? QStringLiteral("parse") : errorKind);
                    return;
                }
                // 响应导航（对照上游 main.py:314-338，形状经实测源码核验）：
                // 根对象 {"data": {...}} 包装 → real/predict；实况字段在
                // real.weather（temperature/feelst/humidity/info）与 real.wind
                // （direct/power）嵌套内；data 缺失 = 数字旧码/接口改版 → 走备源
                const QJsonObject body = doc.object();
                const QJsonObject payload = body.value(QLatin1String("data")).toObject();
                const QJsonObject real = payload.value(QLatin1String("real")).toObject();
                const QJsonObject predict = payload.value(QLatin1String("predict")).toObject();
                const QJsonObject rw = real.value(QLatin1String("weather")).toObject();
                const QJsonObject rwind = real.value(QLatin1String("wind")).toObject();
                const QJsonArray detail = predict.value(QLatin1String("detail")).toArray();
                const QJsonObject today0 = detail.isEmpty() ? QJsonObject() : detail.at(0).toObject();
                const QJsonObject dayW = today0.value(QLatin1String("day")).toObject()
                                             .value(QLatin1String("weather")).toObject();
                const QJsonObject nightW = today0.value(QLatin1String("night")).toObject()
                                               .value(QLatin1String("weather")).toObject();

                QVariantMap current;
                bool gotCurrent = false;
                const double temp = numOf(rw, "temperature", 9999.0);
                double feels = numOf(rw, "feelst", 9999.0);
                const double hum = numOf(rw, "humidity", 9999.0);
                const QString info = rw.value(QLatin1String("info")).toString().trimmed();
                if (!isSentinel(temp) && temp >= -60.0 && temp <= 60.0) {
                    current.insert(QStringLiteral("temperature"), temp);
                    gotCurrent = true;
                }
                // 体感差 >20 疑似华氏则换算；换算后仍需落在温度 ±20 内否则弃用
                // （对照上游 main.py:341-353 的二次校验）
                if (!isSentinel(feels) && current.contains(QStringLiteral("temperature"))) {
                    const double t = current.value(QStringLiteral("temperature")).toDouble();
                    double converted = feels;
                    if (qAbs(feels - t) > 20.0)
                        converted = (feels - 32.0) * 5.0 / 9.0;
                    if (qAbs(converted - t) <= 20.0) {
                        current.insert(QStringLiteral("feelsLike"), converted);
                        gotCurrent = true;
                    }
                }
                if (!isSentinel(hum) && hum >= 0.0 && hum <= 100.0) {
                    current.insert(QStringLiteral("humidity"), hum);
                    gotCurrent = true;
                }
                // 风：direct+power → windScale 文本，windSpeed 留空（NMC 无 m/s）；
                // 9999 哨兵（字符串）置空（上游 main.py:330-337）
                QString wdir = rwind.value(QLatin1String("direct")).toString().trimmed();
                QString wpow = rwind.value(QLatin1String("power")).toString().trimmed();
                if (wdir == QLatin1String("9999"))
                    wdir.clear();
                if (wpow == QLatin1String("9999"))
                    wpow.clear();
                if (!wdir.isEmpty() || !wpow.isEmpty()) {
                    // 与上游展示一致：风向与风力间留空格（单侧为空时 trimmed 收口）
                    current.insert(QStringLiteral("windScale"),
                                   (wdir + QLatin1Char(' ') + wpow).trimmed());
                    gotCurrent = true;
                }
                // 描述：'-' 视为无信息（占位不写 weatherCode，留给备源填补）；
                // 其余非空描述可映射则写码、不可映射写 unknown 占位
                const bool infoUsable = !info.isEmpty() && info != QLatin1String("-");
                if (infoUsable) {
                    const int code = codeFromNmcInfo(info);
                    current.insert(QStringLiteral("weatherCode"), code);
                    gotCurrent = true;
                }

                QVariantMap today;
                bool gotToday = false;
                // 最高/最低温：detail[0].day.weather.temperature 与
                // night.weather.temperature（上游 main.py:326-328/362/377；
                // 夜间白天预报未发布/已归档时 NMC 返回 9999）
                double hi = numOf(dayW, "temperature", 9999.0);
                double lo = numOf(nightW, "temperature", 9999.0);
                // 夜间最高温 9999 用实况兜底 + 当日最高温 day-cache
                // （质检修正：day-cache 只插不删跨进程累积，先清理过期日期条目）
                const QString todayDate = QDate::currentDate().toString(Qt::ISODate);
                for (auto it = m_dayMaxCache.begin(); it != m_dayMaxCache.end();) {
                    if (!it.key().endsWith(todayDate))
                        it = m_dayMaxCache.erase(it);
                    else
                        ++it;
                }
                const QString dayKey = stationCode + todayDate;
                if (current.contains(QStringLiteral("temperature"))) {
                    const double t = current.value(QStringLiteral("temperature")).toDouble();
                    if (isSentinel(hi))
                        hi = t;
                    if (m_dayMaxCache.contains(dayKey))
                        hi = qMax(hi, m_dayMaxCache.value(dayKey));
                    m_dayMaxCache.insert(dayKey, hi);
                }
                // 描述 '-' / 无实况温度时内部备源回退（见 fetchWeatherComCnFallback）
                const bool needFallback = !infoUsable
                    || !current.contains(QStringLiteral("temperature"));
                if (!isSentinel(hi)) {
                    today.insert(QStringLiteral("tempMax"), hi);
                    gotToday = true;
                }
                if (!isSentinel(lo)) {
                    today.insert(QStringLiteral("tempMin"), lo);
                    gotToday = true;
                }
                // 白天/夜间天气描述（day/night.weather.info，形状防御性读取）
                const QString dayInfo = dayW.value(QLatin1String("info")).toString().trimmed();
                const QString nightInfo = nightW.value(QLatin1String("info")).toString().trimmed();
                if (!dayInfo.isEmpty() && dayInfo != QLatin1String("-")) {
                    today.insert(QStringLiteral("dayCode"), codeFromNmcInfo(dayInfo));
                    gotToday = true;
                }
                if (!nightInfo.isEmpty() && nightInfo != QLatin1String("-")) {
                    today.insert(QStringLiteral("nightCode"), codeFromNmcInfo(nightInfo));
                    gotToday = true;
                }
                today.insert(QStringLiteral("precipProb"), 0.0); // NMC 不提供，占位不计入有效性
                if (!isSentinel(hi) && !isSentinel(lo) && hi < lo)
                    today.insert(QStringLiteral("tempMax"), lo); // 强制 hi>=lo

                // 预警：findAlarm 取前 3 页，响应 data.page.list，条目仅
                // title/issuetime，等级从标题文字提取（上游 main.py:180-200）
                auto pageEntries = std::make_shared<QJsonArray>();
                auto pending = std::make_shared<int>(3);
                auto onAlertsDone = [this, city, current, today, gotCurrent, gotToday,
                                     needFallback, pageEntries]() {
                    QVariantList alerts;
                    struct Scored
                    {
                        int score;
                        QVariantMap alert;
                    };
                    QList<Scored> scored;
                    const QString myProvince = stripSuffix(city.province);
                    // 本省标题命中判定：完整主干命中，或省级短名前缀命中
                    // （补充质检修正：stripSuffix 只剥一重后缀，"广西壮族自治区"
                    // →"广西壮族"，标题只含"广西"时须按省级名前缀归一）
                    auto titleHasMyProvince = [&myProvince](const QString &title) {
                        if (title.contains(myProvince))
                            return true;
                        for (const QString &p : kProvinceNames) {
                            if (myProvince.startsWith(p) && title.contains(p))
                                return true;
                        }
                        return false;
                    };
                    for (const QJsonValue &v : std::as_const(*pageEntries)) {
                        const QJsonObject o = v.toObject();
                        const QString title = o.value(QLatin1String("title")).toString();
                        if (!city.name.isEmpty() && !title.contains(city.name))
                            continue; // city in title 过滤
                        // 他省交叉剔除（质检修正：province 参数可能被服务端忽略，
                        // "朝阳"会命中他省"××市朝阳区"标题）：标题不含本省名且
                        // 含其他省级名 → 判为他省预警
                        if (!myProvince.isEmpty() && !titleHasMyProvince(title)) {
                            bool otherProvince = false;
                            for (const QString &p : kProvinceNames) {
                                if (!myProvince.startsWith(p) && title.contains(p)) {
                                    otherProvince = true;
                                    break;
                                }
                            }
                            if (otherProvince)
                                continue;
                        }
                        // 等级从标题提取（上游 _alert_level：红色/橙色/黄色/蓝色/
                        // 白色 → 首字），红 0 < 橙 1 < 黄 2 < 蓝 3 < 白 4：本仓库
                        // rank 反向（红=3 最高）、白色 -1 最低，取前 3 按分数降序
                        // 即"最重 3 条"（质检修正：原把 -1 钳成 0 与蓝色同档）
                        QString level;
                        for (const char *w : { "红色", "橙色", "黄色", "蓝色", "白色" }) {
                            if (title.contains(QString::fromUtf8(w))) {
                                level = QString::fromUtf8(w).left(1);
                                break;
                            }
                        }
                        const int rank = level.isEmpty()
                            ? -1 : cwn::weather::alertLevelRank(level);
                        QVariantMap a;
                        const QString pubTime = o.value(QLatin1String("issuetime")).toString();
                        const QString hash = QString::number(qHash(title + pubTime), 16);
                        a.insert(QStringLiteral("alertId"), QStringLiteral("nmc-") + hash);
                        a.insert(QStringLiteral("type"),
                                 o.value(QLatin1String("type")).toString());
                        a.insert(QStringLiteral("level"),
                                 level.isEmpty() ? QString()
                                                 : cwn::weather::alertLevelLetter(level));
                        a.insert(QStringLiteral("levelRaw"), level);
                        a.insert(QStringLiteral("title"), title);
                        a.insert(QStringLiteral("pubTime"), pubTime);
                        scored.append({ rank, a });
                    }
                    std::sort(scored.begin(), scored.end(),
                              [](const Scored &a, const Scored &b) {
                                  return a.score > b.score;
                              });
                    for (int i = 0; i < qMin(3, scored.size()); ++i)
                        alerts.append(scored.at(i).alert);

                    if (needFallback) {
                        // 备源回包后合并发出（内部方法直接 emit）；alerts 透传
                        // （质检修正：原 fallback 分支丢弃已解析预警，灵动通知不推）
                        fetchWeatherComCnFallback(city, current, today, alerts);
                        return;
                    }
                    WeatherProvider::Snapshot snap;
                    // 有效性按实际解析结果判定（质检修正：原恒 true 把整轮解析
                    // 失败伪装成"成功"，掩盖故障并触发无谓回退）
                    snap.valid = gotCurrent || gotToday;
                    snap.current = current;
                    snap.today = today;
                    snap.alerts = alerts;
                    emit fetchFinished(city.cityId, snap,
                                       snap.valid ? QString() : QStringLiteral("parse"));
                };
                for (int page = 1; page <= 3; ++page) {
                    QUrl aurl(QString::fromLatin1(kNmcBase) + QStringLiteral("/findAlarm"));
                    QUrlQuery aq;
                    aq.addQueryItem(QStringLiteral("pageNo"), QString::number(page));
                    aq.addQueryItem(QStringLiteral("pageSize"), QStringLiteral("50"));
                    if (!city.province.isEmpty())
                        aq.addQueryItem(QStringLiteral("province"), city.province);
                    aurl.setQuery(aq);
                    getJson(aurl, nmcHeaders(), cwn::weather::kFetchTimeoutMs,
                            [pageEntries, pending, onAlertsDone](const QJsonDocument &adoc,
                                                                 const QString &aerr, int) {
                                if (aerr.isEmpty() && adoc.isObject()) {
                                    // data.page.list 三层导航（上游 main.py:190）
                                    const QJsonArray list =
                                        adoc.object().value(QLatin1String("data")).toObject()
                                            .value(QLatin1String("page")).toObject()
                                            .value(QLatin1String("list")).toArray();
                                    for (const QJsonValue &v : list)
                                        pageEntries->append(v);
                                }
                                if (--(*pending) == 0)
                                    onAlertsDone();
                            });
                }
            });
}

void NmcProvider::getText(const QUrl &url,
                          const std::function<void(const QString &, const QString &)> &done)
{
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(cwn::weather::kFetchTimeoutMs);
    // weather.com.cn 备源请求头（补充质检修正：缺 Referer 时 4xx，备源形同虚设）
    for (const auto &h : cnHeaders())
        req.setRawHeader(h.first.toUtf8(), h.second.toUtf8());
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [reply, done] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            done({}, QStringLiteral("network"));
            return;
        }
        done(QString::fromUtf8(reply->readAll()), {});
    });
}

void NmcProvider::fetchWeatherComCnFallback(const CityInfo &city, const QVariantMap &partialCurrent,
                                            const QVariantMap &partialToday,
                                            const QVariantList &alerts)
{
    // 内部备源回退（不拆第 7 源），端点与字段对照上游 main.py:210-263：
    //   toy1/search 查 weather.com.cn 城市码（ref 按 '~' 分段，第 3 段为中文名，
    //     精确匹配优先、去后缀相同次之）→ d1.weather.com.cn/weather_index/{code}.html
    //     （无 dingzhi 前缀；?=时间戳防缓存）文本页解析 `var dataSK = {...}` /
    //     `var cityDZ = {...}`：dataSK 补温度(temp)/湿度(SD)/风(WD WS)/描述(weather)，
    //     cityDZ.weatherinfo 补今日最低温(tempn)。
    // 合并只填缺口不改 NMC 已有值——"温差 >8° 仍用 NMC" 的融合规则因此天然满足；
    // 任何一步失败都保留 NMC 部分原样返回（备源失败绝不丢整轮数据）。
    QUrl surl(QStringLiteral("http://toy1.weather.com.cn/search"));
    QUrlQuery sq;
    sq.addQueryItem(QStringLiteral("cityname"), city.name);
    surl.setQuery(sq);
    getText(surl, [this, city, partialCurrent, partialToday,
                   alerts](const QString &body, const QString &err) {
        QVariantMap current = partialCurrent;
        QVariantMap today = partialToday;
        QString wcnCode;
        if (err.isEmpty() && !body.isEmpty()) {
            // 响应可能带 JSONP 前缀（如 `null[...]`）：截取首个 '[' 到末个 ']'
            const int from = body.indexOf(QLatin1Char('['));
            const int to = body.lastIndexOf(QLatin1Char(']'));
            if (from >= 0 && to > from) {
                const QJsonDocument doc =
                    QJsonDocument::fromJson(body.mid(from, to - from + 1).toUtf8());
                if (doc.isArray()) {
                    QString lenient;
                    for (const QJsonValue &v : doc.array()) {
                        const QString ref =
                            v.toObject().value(QLatin1String("ref")).toString();
                        const QStringList tokens = ref.split(QLatin1Char('~'));
                        if (tokens.isEmpty() || tokens.first().isEmpty())
                            continue;
                        const QString cnName =
                            tokens.size() > 2 ? tokens.at(2)
                                              : (tokens.size() > 1 ? tokens.at(1) : QString());
                        if (cnName == city.name) {
                            wcnCode = tokens.first();
                            break; // 精确匹配优先（上游 main.py:223-226）
                        }
                        if (lenient.isEmpty() && !cnName.isEmpty()
                            && stripSuffix(cnName) == stripSuffix(city.name))
                            lenient = tokens.first(); // 去后缀相同次之（首条）
                    }
                    if (wcnCode.isEmpty())
                        wcnCode = lenient;
                }
            }
        }
        if (wcnCode.isEmpty()) {
            finishFallback(city, current, today, alerts);
            return;
        }
        QUrl durl(QStringLiteral("http://d1.weather.com.cn/weather_index/%1.html").arg(wcnCode));
        QUrlQuery dq;
        dq.addQueryItem(QStringLiteral("_"),
                        QString::number(QDateTime::currentMSecsSinceEpoch()));
        durl.setQuery(dq);
        getText(durl, [this, city, current, today, alerts](const QString &dbody,
                                                           const QString &derr) {
            QVariantMap cur = current;
            QVariantMap day = today;
            if (derr.isEmpty() && !dbody.isEmpty()) {
                // 对照上游 re.S：页面语句可能跨行，`.` 须匹配换行否则整体失配、
                // 备源静默失效（补充质检第 2 轮必修项）
                static const QRegularExpression dataSkRe(
                    QStringLiteral("var\\s*dataSK\\s*=\\s*(\\{.*?\\});"),
                    QRegularExpression::DotMatchesEverythingOption);
                static const QRegularExpression cityDzRe(
                    QStringLiteral("var\\s*cityDZ\\s*=\\s*(\\{.*?\\});"),
                    QRegularExpression::DotMatchesEverythingOption);
                auto varObj = [](const QString &text, const QRegularExpression &re) {
                    const QRegularExpressionMatch m = re.match(text);
                    if (!m.hasMatch())
                        return QJsonObject();
                    return QJsonDocument::fromJson(m.captured(1).toUtf8()).object();
                };
                const QJsonObject sk = varObj(dbody, dataSkRe);
                const QJsonObject dz = varObj(dbody, cityDzRe)
                                           .value(QLatin1String("weatherinfo")).toObject();
                // 数值容错：剥非数字字符（"21"、"21.5"、"78%" 等变体）
                auto numFrom = [](const QString &raw, double fallback) {
                    QString digits;
                    for (QChar c : raw) {
                        if (c.isDigit() || c == QLatin1Char('.') || c == QLatin1Char('-'))
                            digits.append(c);
                        else if (!digits.isEmpty())
                            break;
                    }
                    bool ok = false;
                    const double d = digits.toDouble(&ok);
                    return ok ? d : fallback;
                };
                // dataSK 键名对照上游 main.py:258-261：temp/SD/WD/WS/weather
                if (!cur.contains(QLatin1String("temperature")) && sk.contains(QLatin1String("temp"))) {
                    const double t = numFrom(sk.value(QLatin1String("temp")).toString(), 9999.0);
                    if (!isSentinel(t) && qAbs(t) <= 60.0)
                        cur.insert(QStringLiteral("temperature"), t);
                }
                if (!cur.contains(QLatin1String("humidity")) && sk.contains(QLatin1String("SD"))) {
                    QString sd = sk.value(QLatin1String("SD")).toString();
                    if (sd.endsWith(QLatin1Char('%')))
                        sd.chop(1);
                    const double h = numFrom(sd, 9999.0);
                    if (!isSentinel(h) && h >= 0.0 && h <= 100.0)
                        cur.insert(QStringLiteral("humidity"), h);
                }
                if (!cur.contains(QLatin1String("windScale"))) {
                    // 上游以空格拼接：f"{WD} {WS}".strip()
                    const QString wd = sk.value(QLatin1String("WD")).toString().trimmed();
                    const QString ws = sk.value(QLatin1String("WS")).toString().trimmed();
                    if (!wd.isEmpty() || !ws.isEmpty())
                        cur.insert(QStringLiteral("windScale"), (wd + QLatin1Char(' ') + ws).trimmed());
                }
                if (!cur.contains(QLatin1String("weatherCode")) && sk.contains(QLatin1String("weather")))
                    cur.insert(QStringLiteral("weatherCode"),
                               codeFromNmcInfo(sk.value(QLatin1String("weather")).toString()));
                // cityDZ.weatherinfo.tempn = 今日最低温（上游 main.py:262；无今日
                // 最高温键，tempMax 不从备源补）
                if (!day.contains(QLatin1String("tempMin")) && dz.contains(QLatin1String("tempn"))) {
                    const double t = numFrom(dz.value(QLatin1String("tempn")).toString(), 9999.0);
                    if (!isSentinel(t) && qAbs(t) <= 60.0)
                        day.insert(QStringLiteral("tempMin"), t);
                }
                if (day.contains(QLatin1String("tempMax"))
                    && day.contains(QLatin1String("tempMin"))
                    && day.value(QLatin1String("tempMax")).toDouble()
                        < day.value(QLatin1String("tempMin")).toDouble())
                    day.insert(QLatin1String("tempMax"), day.value(QLatin1String("tempMin")));
            }
            finishFallback(city, cur, day, alerts);
        });
    });
}

void NmcProvider::finishFallback(const CityInfo &city, const QVariantMap &current,
                                 const QVariantMap &today, const QVariantList &alerts)
{
    WeatherProvider::Snapshot snap;
    // 有效性按实义字段判定（snapshotHasData 排除 precipProb 占位）——补充质检
    // 修正：原 map 非空判 valid 使零数据快照伪装 ok 并抑制重试
    snap.valid = snapshotHasData(current, today);
    snap.current = current;
    snap.today = today;
    snap.alerts = alerts;
    emit fetchFinished(city.cityId, snap, snap.valid ? QString() : QStringLiteral("parse"));
}

QString NmcProvider::indexFilePath() const
{
    // 落盘 configs/weather/nmc_station_index.json（AppPaths::configsRoot 运行时
    // 解析；质检修正：原 QDir::current 相对路径 + 开发树探测 hack 在打包运行
    // 时 cwd 不可控，缓存读写位置漂移）
    return AppPaths::instance().configsRoot() + QString::fromLatin1(kIndexFileTail);
}

void NmcProvider::loadIndexFromDisk()
{
    QFile file(indexFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    const QJsonObject root = doc.object();
    // version 字段不符（旧格式/损坏）→ 丢弃走在线重建
    if (root.value(QLatin1String("version")).toInt(0) != kIndexVersion
        || !root.value(QLatin1String("stations")).isArray())
        return;
    for (const QJsonValue &v : root.value(QLatin1String("stations")).toArray()) {
        const QJsonObject o = v.toObject();
        StationEntry e{ o.value(QLatin1String("city")).toString(),
                       o.value(QLatin1String("code")).toString(),
                       o.value(QLatin1String("province")).toString() };
        if (e.city.isEmpty() || e.code.isEmpty())
            continue;
        // 纯数字 code 非字母站号：旧格式/污染数据不采信（该城市走在线重建）
        bool digits = !e.code.isEmpty();
        for (QChar c : e.code) {
            if (!c.isDigit()) {
                digits = false;
                break;
            }
        }
        if (digits)
            continue;
        m_stations.append(e);
    }
    if (!m_stations.isEmpty())
        m_indexReady = true;
}

void NmcProvider::saveIndexToDisk() const
{
    if (m_stations.isEmpty())
        return;
    const QString path = indexFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        cwn::Log::warn(QStringLiteral("NmcProvider: cannot write station index: %1").arg(path));
        return;
    }
    QJsonArray arr;
    for (const StationEntry &e : m_stations) {
        QJsonObject o;
        o.insert(QStringLiteral("city"), e.city);
        o.insert(QStringLiteral("code"), e.code);
        o.insert(QStringLiteral("province"), e.province);
        arr.append(o);
    }
    QJsonObject root;
    root.insert(QStringLiteral("version"), kIndexVersion);
    root.insert(QStringLiteral("stations"), arr);
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}
