/*
 * ⚠️ 本文件已退役（阶段 B 天气迁移，extensions-feature-plan §5 B5，2026-10）。
 *
 * 取代者：pages/settings/Extensions/Weather.qml（全局「扩展功能-天气」配置页）。
 * 退役原因：天气从「每个实例右键设置」改为「扩展开关 + 全局配置」——
 *   1) BuiltinWidgets.cpp 的天气定义不再设置 settingsQml，右键设置入口在
 *      WidgetsContainer.qml 中因 model.settingsQml 为空而禁用，本页已无任何
 *      引用路径（grep 全仓库除本文件自身与注释外零引用）；
 *   2) 城市键从实例 settings.city 收敛为全局 weather.city，组件本体
 *      （widgets/weather.qml）改读 Configs.data.weather.city，本文件按实例
 *      settings 读写城市的逻辑已与组件数据源脱节，保留可运行状态仅为对照。
 *
 * 保留不删的理由：作为迁移源与回退参考（计划允许"保留文件待清理"）；如需
 * 复活本页须同时恢复 BuiltinWidgets 的 settingsQml 并把城市读写键改回实例
 * settings.city，否则城市配置与组件实际读取的全局键不一致。后续阶段清理时
 * 连同 defaultSettings 的 city 残留键一并移除。
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RinUI
import ClassWidgets.Plugins

SettingsLayout {
    id: root

    // 注入属性：settings / instanceId / widget_id（SettingsLayout 自带 settings 声明）
    // settings.city 为城市 JSON 字符串，契约见 WeatherService
    property var cityValue: {
        try {
            return (settings && settings.city) ? JSON.parse(settings.city) : null
        } catch (e) {
            return null
        }
    }
    property var searchResults: []
    // "测试连接"结果文案（AppCentral.weather.connectionTestFinished 回填）
    property string testStatus: ""

    // 数据源注册表：weather.provider 配置值；label 覆盖"收费"标注（产品决策：
    // 选中即视为知情，无额外计费提示）；note 展示在卡片描述
    readonly property var sources: [
        { id: "xiaomi", label: qsTr("Xiaomi Weather (Free)"),
          note: qsTr("Built-in, no configuration required") },
        { id: "amap", label: qsTr("AMap Weather (Paid)"),
          note: qsTr("Requires an AMap Web service key") },
        { id: "qweather", label: qsTr("QWeather (Paid)"),
          note: qsTr("Requires a QWeather API key and dedicated API host") },
        { id: "weathercn", label: qsTr("WeatherCN (Paid)"),
          note: qsTr("Requires a WeatherCN API key") },
        { id: "caiyun", label: qsTr("Caiyun Weather (Paid)"),
          note: qsTr("Requires a Caiyun API token") }
    ]
    readonly property string providerId: {
        const saved = Configs.data.weather ? Configs.data.weather.provider : ""
        for (let i = 0; i < sources.length; i++)
            if (sources[i].id === saved) return saved
        return "xiaomi"
    }
    // 各源凭据已存的键（凭据卡按当前源动态显示对应字段）
    readonly property bool needsCredentials: providerId !== "xiaomi"

    function readKey(sourceId, field) {
        const w = Configs.data.weather
        const keys = w ? w.keys : null
        const s = keys ? keys[sourceId] : null
        return (s && s[field]) ? s[field] : ""
    }

    function selectCity(city) {
        // 整体重赋 settings（而非改成员），让 cityValue 绑定随属性变更重算刷新描述；
        // 对话框 Ok 时 WidgetsModel.updateSettings 会持久化该对象（上游同契约）。
        // 城市携带数据源扩展键（adcode/wcnKey），换数据源时免重选城市。
        settings = Object.assign({}, settings || {}, {
            city: JSON.stringify({
                cityId: city.cityId,
                name: city.name,
                lat: city.lat,
                lon: city.lon,
                province: city.province || "",
                adcode: city.adcode || "",
                wcnKey: city.wcnKey || ""
            })
        })
        root.searchResults = []
        searchField.text = ""
    }

    Connections {
        target: AppCentral.weather
        function onCitySearchFinished(cities) {
            root.searchResults = cities
        }
        function onConnectionTestFinished(ok, errorKind) {
            if (ok)
                root.testStatus = qsTr("Connection successful")
            else if (errorKind === "auth")
                root.testStatus = qsTr("Invalid API key")
            else if (errorKind === "quota")
                root.testStatus = qsTr("Quota exceeded")
            else if (errorKind === "network")
                root.testStatus = qsTr("Network error")
            else
                root.testStatus = qsTr("Unexpected response")
            // 测试通过且当时选中的就是当前源 → 立即按新凭据重拉
            if (ok)
                AppCentral.weather.applyConfigChange()
        }
    }

    SettingCard {
        Layout.fillWidth: true

        icon.name: "ic_fluent_weather_sunny_20_regular"
        title: qsTr("Data source")
        description: {
            let src = root.sources[0]
            for (let i = 0; i < root.sources.length; i++)
                if (root.sources[i].id === root.providerId) src = root.sources[i]
            let text = src.note
            // 高德无预警接口：一行小字提示（预警走灵动通知）
            if (root.providerId === "amap")
                text += " · " + qsTr("AMap does not support weather alerts yet")
            return text
        }

        ComboBox {
            property var values: ["xiaomi", "amap", "qweather", "weathercn", "caiyun"]
            model: ListModel {
                ListElement { text: qsTr("Xiaomi Weather (Free)") }
                ListElement { text: qsTr("AMap Weather (Paid)") }
                ListElement { text: qsTr("QWeather (Paid)") }
                ListElement { text: qsTr("WeatherCN (Paid)") }
                ListElement { text: qsTr("Caiyun Weather (Paid)") }
            }
            Component.onCompleted: {
                // 命令式初始化（对齐 countdownDays 先例）：读保存值定位
                var i = values.indexOf(root.providerId)
                currentIndex = i >= 0 ? i : 0
            }
            onActivated: {
                Configs.set("weather.provider", values[currentIndex])
                // 切源后刷新凭据输入框（防止旧源值被写进新源的键）并清测试态
                credentialField.syncFromConfig()
                hostField.syncFromConfig()
                root.testStatus = ""
                if (AppCentral.weather)
                    AppCentral.weather.applyConfigChange()
            }
        }
    }

    // 凭据卡（仅非小米源显示）：key/token/host 保存到 weather.keys.<source>.*
    SettingCard {
        visible: root.needsCredentials
        Layout.fillWidth: true

        icon.name: "ic_fluent_key_20_regular"
        title: qsTr("API credentials")
        description: root.testStatus.length > 0
            ? root.testStatus
            : qsTr("Stored locally in configs.json")

        ColumnLayout {
            spacing: 6

            RowLayout {
                spacing: 6

                TextField {
                    id: credentialField
                    Layout.preferredWidth: 220
                    echoMode: showToggle.checked ? TextInput.Normal : TextInput.Password
                    placeholderText: {
                        if (root.providerId === "amap") return qsTr("AMap Key")
                        if (root.providerId === "qweather") return qsTr("QWeather Key")
                        if (root.providerId === "weathercn") return qsTr("WeatherCN Key")
                        return qsTr("Caiyun Token")
                    }
                    function syncFromConfig() {
                        text = root.readKey(root.providerId, "key")
                    }
                    Component.onCompleted: syncFromConfig()
                    onEditingFinished: {
                        Configs.set("weather.keys." + root.providerId + ".key", text)
                    }
                }

                TextField {
                    id: hostField
                    visible: root.providerId === "qweather"
                    Layout.preferredWidth: 200
                    placeholderText: qsTr("API Host") + " (xxx.qweatherapi.com)"
                    function syncFromConfig() {
                        text = root.readKey("qweather", "host")
                    }
                    Component.onCompleted: syncFromConfig()
                    onEditingFinished: Configs.set("weather.keys.qweather.host", text.trim())
                }
            }

            RowLayout {
                spacing: 6

                CheckBox {
                    id: showToggle
                    text: qsTr("Show")
                }

                Button {
                    text: qsTr("Test connection")
                    onClicked: {
                        root.testStatus = qsTr("Testing…")
                        AppCentral.weather.testConnection()
                    }
                }
            }
        }
    }

    SettingCard {
        Layout.fillWidth: true

        icon.name: "ic_fluent_location_20_regular"
        title: qsTr("City")
        description: root.cityValue
            ? root.cityValue.name
              + (root.cityValue.province ? " · " + root.cityValue.province : "")
            : qsTr("Search and select a city to enable weather")

        TextField {
            id: searchField
            Layout.preferredWidth: 180
            placeholderText: qsTr("Search city")
            onTextChanged: searchTimer.restart()
        }
    }

    // 搜索结果（350ms 防抖 + name · province 列表，对齐上游城市搜索交互；
    // 搜索路由到当前数据源，彩云无搜索接口时由服务侧落到小米）
    ColumnLayout {
        visible: root.searchResults.length > 0
        Layout.fillWidth: true
        spacing: 4

        Timer {
            id: searchTimer
            interval: 350
            onTriggered: {
                if (searchField.text.trim().length > 0 && AppCentral.weather)
                    AppCentral.weather.searchCity(searchField.text)
                else
                    root.searchResults = []
            }
        }

        Repeater {
            model: Math.min(root.searchResults.length, 6)

            delegate: Button {
                required property int index
                Layout.fillWidth: true
                text: root.searchResults[index].name
                    + (root.searchResults[index].province
                        ? " · " + root.searchResults[index].province : "")
                onClicked: root.selectCity(root.searchResults[index])
            }
        }
    }

    SettingCard {
        Layout.fillWidth: true

        icon.name: "ic_fluent_arrow_clockwise_20_regular"
        title: qsTr("Refresh interval")
        description: qsTr("How often to fetch weather data (30–180 minutes)")

        SpinBox {
            from: 30
            to: 180
            stepSize: 30
            textFromValue: (value) => value + qsTr(" min")
            valueFromText: (text) => parseInt(text) || 60
            onValueModified: Configs.set("weather.poll_interval", value * 60)
            Component.onCompleted: {
                // 全局配置（weather.poll_interval，秒）；服务每次唤醒重读，改动立即生效
                const saved = (Configs.data.weather
                               && Configs.data.weather.poll_interval) || 3600
                value = Math.min(180, Math.max(30, Math.round(saved / 60)))
            }
        }
    }

    SettingCard {
        Layout.fillWidth: true

        icon.name: "ic_fluent_info_20_regular"
        title: qsTr("Attribution")
        description: {
            if (root.providerId === "amap")
                return qsTr("Weather data from AMap")
            if (root.providerId === "qweather")
                return qsTr("Weather data from QWeather")
            if (root.providerId === "weathercn")
                return qsTr("Weather data from WeatherCN")
            if (root.providerId === "caiyun")
                return qsTr("Weather data from Caiyun")
            return qsTr("Weather data from Xiaomi Weather (wtr-v3)")
        }
    }
}
