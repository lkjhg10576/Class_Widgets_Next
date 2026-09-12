import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RinUI
import ClassWidgets.Theme
import Qt5Compat.GraphicalEffects

Widget {
    id: root
    // 不设 header 文本（对齐 Time.qml 先例）：常态高 100px 中 header 会占去内容区，
    // 天气两行内容（温度 + 现象文本）需要完整内容区居中展示
    // text: qsTr("Weather")

    // backend = WeatherService（AppCentral::registerBuiltinWidgets 为本组件注入专属数据源）。
    // settings.city 为城市 JSON 字符串：{"cityId","name","lat","lon","province"}
    readonly property string cityJson: (settings && settings.city) ? settings.city : ""
    readonly property bool cityConfigured: cityJson.length > 0
    property var weatherInfo: null

    readonly property bool hasData: weatherInfo && weatherInfo.status !== "empty"
    readonly property var current: hasData ? weatherInfo.current : null
    readonly property var today: hasData ? weatherInfo.today : null

    // 最高等级预警（等级归一 + 配色对齐上游 level_to_letter 与等级色板）
    readonly property var topAlert: {
        if (!hasData || !weatherInfo.alerts || weatherInfo.alerts.length === 0)
            return null
        const rank = { "B": 0, "Y": 1, "O": 2, "R": 3 }
        let best = null
        for (let i = 0; i < weatherInfo.alerts.length; i++) {
            const a = weatherInfo.alerts[i]
            if (!best || rank[alertLetter(a.level)] > rank[alertLetter(best.level)])
                best = a
        }
        return best
    }

    function alertLetter(raw) {
        if (raw === "Y" || raw === "黄色") return "Y"
        if (raw === "O" || raw === "橙色") return "O"
        if (raw === "R" || raw === "红色") return "R"
        return "B"
    }

    // 天气现象代码 → 文本：对齐上游 weather_code_to_text 逐条表；数据源 locale 固定
    // zh_cn（预警 type 等字段本身即中文），故此处用中文常量而非 qsTr 英文源
    function weatherText(code) {
        const map = {
            0: "晴", 1: "多云", 2: "阴", 3: "阵雨", 4: "雷阵雨", 5: "雷阵雨伴有冰雹",
            6: "雨夹雪", 7: "小雨", 8: "中雨", 9: "大雨", 10: "暴雨", 11: "大暴雨",
            12: "特大暴雨", 13: "阵雪", 14: "小雪", 15: "中雪", 16: "大雪", 17: "暴雪",
            18: "雾", 19: "冻雨", 20: "沙尘暴", 21: "小到中雨", 22: "中到大雨",
            23: "大到暴雨", 24: "暴雨到大暴雨", 25: "大暴雨到特大暴雨", 26: "小到中雪",
            27: "中到大雪", 28: "大到暴雪", 32: "浓雾", 35: "轻雾", 49: "强浓雾",
            53: "霾", 54: "中度霾", 55: "重度霾", 56: "严重霾", 57: "大雾", 58: "特强浓雾",
            301: "雨", 302: "雪"
        }
        return map[code] || "未知"
    }

    function reload() {
        weatherInfo = (backend && cityConfigured) ? backend.weatherData(cityJson) : null
    }

    onCityJsonChanged: {
        if (backend && cityConfigured)
            backend.request(cityJson) // 缺数据或已过期才真正发起拉取
        reload()
    }

    Connections {
        target: backend
        function onWeatherUpdated(cityId) {
            root.reload()
        }
    }

    Component.onCompleted: reload()

    RowLayout {
        anchors.centerIn: parent
        spacing: 10

        Icon {
            // 有数据用服务按昼夜解析的图标；未就绪用中性云图标占位
            icon: root.current && root.current.icon
                  ? root.current.icon
                  : "ic_fluent_weather_cloudy_20_regular"
            size: miniMode ? 24 : 32
        }

        ColumnLayout {
            spacing: 0
            Layout.alignment: Qt.AlignVCenter

            RowLayout {
                spacing: 4

                Title {
                    text: root.current
                        ? Math.round(root.current.temperature) + "°"
                        : "--°"
                }

                Icon {
                    visible: root.topAlert !== null
                    icon: "ic_fluent_warning_20_regular"
                    size: miniMode ? 14 : 16
                    color: root.topAlert
                        ? ({
                            B: "#3b82f6", Y: "#eab308", O: "#f97316", R: "#ef4444"
                        })[root.alertLetter(root.topAlert.level)] || "#eab308"
                        : "transparent"
                }
            }

            Subtitle {
                visible: !miniMode
                text: {
                    if (!root.cityConfigured)
                        return qsTr("Right-click to set a city")
                    if (!root.current)
                        return qsTr("Loading…")
                    let line = weatherText(root.current.weatherCode)
                    if (root.today)
                        line += " · " + Math.round(root.today.tempMax) + "° / "
                                + Math.round(root.today.tempMin) + "°"
                    return line
                }
            }
        }
    }
}
