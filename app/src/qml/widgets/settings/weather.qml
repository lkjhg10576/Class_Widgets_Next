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

    function selectCity(city) {
        // 整体重赋 settings（而非改成员），让 cityValue 绑定随属性变更重算刷新描述；
        // 对话框 Ok 时 WidgetsModel.updateSettings 会持久化该对象（上游同契约）
        settings = Object.assign({}, settings || {}, {
            city: JSON.stringify({
                cityId: city.cityId,
                name: city.name,
                lat: city.lat,
                lon: city.lon,
                province: city.province || ""
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

    // 搜索结果（350ms 防抖 + name · province 列表，对齐上游城市搜索交互）
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
        title: qsTr("Data source")
        description: qsTr("Weather data from Xiaomi Weather (wtr-v3)")
    }
}
