# 天气组件多数据源适配 — 实现定稿（已实施）

> 状态：**已实施并通过全量编译 + 离屏冒烟验证**（2026-09-27）。
> 默认源小米天气经真实拉取回归：`Weather updated for 101010100 (xiaomi, current=ok, alerts=0)`。
> 高德/和风/华风爱科/彩云四个收费源待用户填入各自凭据后实测。

## 需求定稿（用户确认）

1. 默认数据源 = 小米天气，行为与原先一致。
2. 非小米四源在数据源选择处一律标注"收费"；选中即视为知情并已付费，UI 无额外计费/试用/配额提示。
3. AQI/PM2.5 不做（小米现状保留）。
4. 气象预警不在天气组件上显示，推送到**灵动通知小组件**（`dynamicNotification.qml` / 浮窗 `FloatingWidget.qml`）。
5. 选中高德时数据源卡描述追加一行小字："高德天气暂不支持预警"。
6. 预警推送策略：一次拉取新增多条时**只推最高等级**（B<Y<O<R）；**启动首次拉取也推送**；会话内按 alertId 去重（重启会重推当前生效预警；同一预警内容更新不重推，已知限制）。

## 最终代码结构

```
src/core/weather/
  WeatherService.{h,cpp}   门面：60s 轮询/"间隔90%"过期/在途去重/内存缓存/
                           auth·quota 冷却退避（2×间隔封顶6h）/预警→灵动通知推送
                           （NotificationProvider id: com.classwidgets.weather.alerts）
  WeatherProvider.{h,cpp}  抽象：CityInfo(含 adcode/wcnKey 扩展键)、Snapshot、
                           getJson 公共 GET-JSON、HTTP→errorKind 映射、readDouble
  WeatherCodes.{h,cpp}     规范天气码（=小米代码集，QML weatherText 直接消费）+
                           四源映射表（高德中文文本/和风数字码/华风Icon1-47/彩云skycon）+
                           iconForCode（自旧 weatherCodeToIcon 平移）+
                           alertLevelRank/alertLevelLetter
  providers/
    XiaomiProvider         weather/all 合并接口；location/city/search（行为不变）
    AmapProvider           weatherInfo base+all 双请求聚合；geocode/geo 搜索（batch）；
                           regeo 由经纬度解析 adcode（会话缓存）；无预警/AQI/日出日落/
                           降水概率→留空；风力等级文本存 windScale；恒 HTTP200，
                           body status/infocode→auth/quota
    QweatherProvider       专属 Host + X-QW-Api-Key；current+daily+alert 三请求聚合
                           （alert 失败可降级）；湿度/降水概率 0-1→×100；color.code→B/Y/O/R；
                           localTime=true 使日出日落可 mid(11,5) 取 HH:MM
    WeathercnProvider      X-Gw-API-Key 头；translate/geoposition 定位（Key 会话缓存）；
                           current+5day+alerts 三请求聚合；Metric 单位，风速 km/h÷3.6；
                           AlertID/Level/AlarmLevel/Description.Localized/Area[].StartTime
    CaiyunProvider         v2.6 综合接口单请求（token 嵌 URL，经度在前）；
                           skycon 按昼/夜取 08h_20h/20h_32h 档；无城市搜索（门面路由到小米）
```

## 配置键（configs.json，全局）

```
weather.provider = "xiaomi"（默认）| "amap" | "qweather" | "weathercn" | "caiyun"
weather.poll_interval = 秒（原样保留，1800-10800）
weather.keys.amap.key
weather.keys.qweather.key
weather.keys.qweather.host          # xxx.qweatherapi.com
weather.keys.weathercn.key
weather.keys.caiyun.token
```

## QML 契约变化（最小集）

- `weatherData().status` 新增 **"unconfigured"**（已选城市但当前源缺凭据）；
  其余键与旧契约一致（`alerts` 仍返回但组件不渲染）。
- `weather.qml`：摘除 topAlert/alertLetter/预警配色图标块；副标题新增
  "请到小组件设置中配置 API 密钥" 分支。
- `settings/weather.qml`：数据源 ComboBox（含收费标注、默认小米、高德预警小字）、
  凭据卡（key/host 输入 + 显隐 + 测试连接，连接成功即 applyConfigChange 重拉）、
  城市卡（搜索随源路由，彩云落小米）、刷新间隔卡（原样）、归属卡。
- 城市序列化新增可选键 `adcode`（高德）/`wcnKey`（华风），旧数据兼容。
- `zh_CN.ts` 新增 `weather`/`WeatherService` 上下文共 42 条翻译，`zh_CN.qm` 已用
  lrelease 重编（Qt: E:\QtMain\6.10.3\msvc2022_64）。

## 归一单位约定（Provider → QML）

| 字段 | 规范 |
|---|---|
| 温度 | °C double |
| humidity / precipProb | 0–100 |
| windSpeed | m/s（华风 km/h÷3.6；高德只有风力等级→windScale 文本，windSpeed 留空） |
| weatherCode/dayCode/nightCode | 规范枚举（小米集）；未映射 = -1 → QML "未知"+sun 兜底 |
| alerts[].level | B/Y/O/R（levelRaw 存原文：中文颜色/英文颜色码） |
| sunrise/sunset | "HH:MM" |
| pm25/aqi | 仅小米源提供 |

## 各源 API 要点（实现依据）

- **小米 wtr-v3**：`weather/all` 单请求合并（appKey/sign 固定常量）；预警 level 原样 B/Y/O/R 或中文。
- **高德**：`restapi.amap.com/v3/weather/weatherInfo?city=adcode&extensions=base|all`（每轮 2 请求，
  月配额 5,000）；`geocode/geo?batch=true` 搜索、`geocode/regeo` 坐标→adcode（基础 LBS 配额）；
  weather 只给中文文本 → WeatherCodes 映射；无预警接口（选中时 UI 提示）。
- **和风 v1**：`https://{专属Host}/weather/v1/current|daily/{lat}/{lon}`、`/weatheralert/v1/current/...`、
  `/geo/v2/city/lookup`；API Key 头 `X-QW-Api-Key`（JWT/Ed25519 为后续增强，应对 2027-01-01 起
  API Key 限额）；错误体 code 401/403→auth、402/429→quota；`metadata.attributions` 需随数据展示
  （设置页归属卡承载）。
- **华风爱科**：`openapi.weathercn.com`（AccuWeather 风格 PascalCase，双单位）；
  `/locations/v1/cities/translate|geoposition/search.json`、`/currentconditions/v1/{key}.json`、
  `/forecasts/v1/daily/5day/{key}.json`、`/alerts/v1/{key}.json`（数组，无预警时 `[]`）。
- **彩云**：`api.caiyunapp.com/v2.6/{token}/{lon},{lat}/weather?alert=true&dailysteps=3&hourlysteps=24`
  （URL 经度在前，响应 location 是 [纬度,经度]）；`{"status":"failed","error":...}` 含
  invalid/token→auth，其余→quota；alert 块需 token 权限，缺失时留空。

## 已知限制 / 后续增强（未实施）

- 和风 JWT/Ed25519 鉴权（API Key 模式 2027-01-01 起受限）。
- 快照落盘缓存（付费源重启免重拉）；OS 凭据管理（DPAPI）存 key。
- 高德/华风 AQI、和风 air-quality 接口补齐 pm25/aqi。
- 小米源失效自动降级到已配置备用源。
- 同一预警 alertId 内容更新不重推；不同城市同名预警依赖 alertId 区分。
