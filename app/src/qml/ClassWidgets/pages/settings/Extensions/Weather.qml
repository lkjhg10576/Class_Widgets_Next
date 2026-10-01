import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RinUI

// 天气扩展配置页（阶段 B，extensions-feature-plan §5 B5）：内容整体迁自
// widgets/settings/weather.qml（原为右键「小组件设置」的每实例对话框页，入口
// 已随天气扩展化退役）。城市从每实例 settings.city 收敛为全局 weather.city，
// 与组件 widgets/weather.qml 同源读写；数据源/凭据/刷新间隔本就是全局键
// （weather.provider / weather.keys.* / weather.poll_interval，见
// weather-multi-provider-plan.md），原样迁移。页结构照 Time.qml 范式：
// FluentPage + 分组标题 Text + SettingCard 组。
FluentPage {
    id: root

    horizontalPadding: 0
    wrapperWidth: Math.min(width - 42 * 2, 1000)

    title: qsTr("天气")

    // 全局城市（weather.city，JSON 字符串），解析方式对齐原页对 settings.city 的
    // try/JSON.parse 惯例：坏数据回退 null 而非抛错卡死页面
    property var cityValue: {
        try {
            const json = Configs.data.weather ? Configs.data.weather.city : ""
            return json ? JSON.parse(json) : null
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
        { id: "xiaomi", label: qsTr("小米天气（免费）"),
          note: qsTr("内置，无需配置") },
        { id: "amap", label: qsTr("高德天气（付费）"),
          note: qsTr("需要高德开放平台 Web 服务 Key") },
        { id: "qweather", label: qsTr("和风天气（付费）"),
          note: qsTr("需要和风天气 API Key 与专属 API Host") },
        { id: "weathercn", label: qsTr("华风爱科（付费）"),
          note: qsTr("需要华风爱科 API Key") },
        { id: "caiyun", label: qsTr("彩云天气（付费）"),
          note: qsTr("需要彩云天气 Token") }
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
        // 阶段 B 收敛：城市写全局 weather.city（不再经实例 settings）。落盘触发
        // Configs.dataChanged → 组件侧 cityJson 绑定重算 → 组件自动按新城市拉取，
        // 页面无需手动通知服务。城市携带数据源扩展键（adcode/wcnKey），换数据源
        // 时免重选城市（契约同 WeatherService::cityFromJson）。
        Configs.set("weather.city", JSON.stringify({
            cityId: city.cityId,
            name: city.name,
            lat: city.lat,
            lon: city.lon,
            province: city.province || "",
            adcode: city.adcode || "",
            wcnKey: city.wcnKey || ""
        }))
        root.searchResults = []
        searchField.text = ""
        // 选好城市后立即按当前数据源拉取一次（组件存在时 request 亦会触发，
        // 服务侧 fetchNow 的在途去重保证不会重复发起）
        if (AppCentral.weather)
            AppCentral.weather.applyConfigChange()
    }

    Connections {
        target: AppCentral.weather
        function onCitySearchFinished(cities) {
            root.searchResults = cities
        }
        function onConnectionTestFinished(ok, errorKind) {
            if (ok)
                root.testStatus = qsTr("连接成功")
            else if (errorKind === "auth")
                root.testStatus = qsTr("API Key 无效")
            else if (errorKind === "quota")
                root.testStatus = qsTr("配额已用尽")
            else if (errorKind === "network")
                root.testStatus = qsTr("网络错误")
            else
                root.testStatus = qsTr("响应异常")
            // 测试通过且当时选中的就是当前源 → 立即按新凭据重拉
            if (ok)
                AppCentral.weather.applyConfigChange()
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 4

        Text {
            typography: Typography.BodyStrong
            text: qsTr("天气")
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 4
        }

        SettingCard {
            Layout.fillWidth: true

            icon.name: "ic_fluent_weather_sunny_20_regular"
            title: qsTr("数据源")
            description: {
                let src = root.sources[0]
                for (let i = 0; i < root.sources.length; i++)
                    if (root.sources[i].id === root.providerId) src = root.sources[i]
                let text = src.note
                // 高德无预警接口：一行小字提示（预警走灵动通知）
                if (root.providerId === "amap")
                    text += " · " + qsTr("高德暂不支持恶劣天气预警")
                return text
            }

            ComboBox {
                property var values: ["xiaomi", "amap", "qweather", "weathercn", "caiyun"]
                model: ListModel {
                    ListElement { text: qsTr("小米天气（免费）") }
                    ListElement { text: qsTr("高德天气（付费）") }
                    ListElement { text: qsTr("和风天气（付费）") }
                    ListElement { text: qsTr("华风爱科（付费）") }
                    ListElement { text: qsTr("彩云天气（付费）") }
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
            title: qsTr("API 凭据")
            description: root.testStatus.length > 0
                ? root.testStatus
                : qsTr("仅保存在本地 configs.json")

            ColumnLayout {
                spacing: 6

                RowLayout {
                    spacing: 6

                    TextField {
                        id: credentialField
                        Layout.preferredWidth: 220
                        echoMode: showToggle.checked ? TextInput.Normal : TextInput.Password
                        placeholderText: {
                            if (root.providerId === "amap") return qsTr("高德 Key")
                            if (root.providerId === "qweather") return qsTr("和风天气 Key")
                            if (root.providerId === "weathercn") return qsTr("华风爱科 Key")
                            return qsTr("彩云天气 Token")
                        }
                        function syncFromConfig() {
                            text = root.readKey(root.providerId, "key")
                        }
                        Component.onCompleted: syncFromConfig()
                        onEditingFinished: {
                            Configs.set("weather.keys." + root.providerId + ".key", text)
                            // 凭据就位后立即按当前源拉取一次（否则要等 60s 轮询
                            // 或重启才生效）；服务侧对本源无城市活动时也会登记
                            // weather.city，保证组件未 request 过也能拿到数据
                            if (AppCentral.weather)
                                AppCentral.weather.applyConfigChange()
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
                        onEditingFinished: {
                            Configs.set("weather.keys.qweather.host", text.trim())
                            if (AppCentral.weather)
                                AppCentral.weather.applyConfigChange()
                        }
                    }
                }

                RowLayout {
                    spacing: 6

                    CheckBox {
                        id: showToggle
                        text: qsTr("显示")
                    }

                    Button {
                        text: qsTr("测试连接")
                        onClicked: {
                            root.testStatus = qsTr("测试中…")
                            AppCentral.weather.testConnection()
                        }
                    }
                }
            }
        }

        SettingCard {
            Layout.fillWidth: true

            icon.name: "ic_fluent_location_20_regular"
            title: qsTr("城市")
            description: root.cityValue
                ? root.cityValue.name
                  + (root.cityValue.province ? " · " + root.cityValue.province : "")
                : qsTr("搜索并选择城市以启用天气")

            TextField {
                id: searchField
                Layout.preferredWidth: 180
                placeholderText: qsTr("搜索城市")
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
            title: qsTr("刷新间隔")
            description: qsTr("天气数据拉取频率（30–180 分钟）")

            SpinBox {
                from: 30
                to: 180
                stepSize: 30
                textFromValue: (value) => value + qsTr(" 分钟")
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
            title: qsTr("数据来源")
            description: {
                if (root.providerId === "amap")
                    return qsTr("天气数据来自高德地图")
                if (root.providerId === "qweather")
                    return qsTr("天气数据来自和风天气")
                if (root.providerId === "weathercn")
                    return qsTr("天气数据来自华风爱科")
                if (root.providerId === "caiyun")
                    return qsTr("天气数据来自彩云天气")
                return qsTr("天气数据来自小米天气 (wtr-v3)")
            }
        }
    }
}
