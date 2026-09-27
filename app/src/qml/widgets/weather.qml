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
    // 气象预警不再在本组件展示：由 WeatherService 推送到灵动通知小组件

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

    // 注意：以下两处判断必须直接读 cityJson，不能用派生属性 cityConfigured。
    // QML 在 cityJsonChanged 处理器执行期间，依赖 cityJson 的绑定（cityConfigured）
    // 尚未失效重算，读到的仍是旧值 false —— 会导致 request() 永不发出、
    // 组件永远停在 "Loading…"（m_activeCities 空 → 60s 轮询也不会补拉）。
    function reload() {
        weatherInfo = (backend && cityJson.length > 0) ? backend.weatherData(cityJson) : null
    }

    onCityJsonChanged: {
        if (backend && cityJson.length > 0)
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
            }

            Subtitle {
                visible: !miniMode
                text: {
                    if (!root.cityConfigured)
                        return qsTr("Right-click to set a city")
                    if (hasData && weatherInfo.status === "unconfigured")
                        return qsTr("Set API key in widget settings")
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
