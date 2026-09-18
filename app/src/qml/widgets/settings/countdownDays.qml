import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RinUI
import ClassWidgets.Plugins

SettingsLayout {
    id: root

    // 注入属性：settings / instanceId / widget_id（SettingsLayout 自带 settings 声明）
    // settings 契约见 widgets/countdownDays.qml：title 事件名、target_date "yyyy-MM-dd"

    readonly property string targetDateStr:
        (root.settings && root.settings.target_date) ? root.settings.target_date : ""
    readonly property bool targetDateSet: root.targetDateStr.length > 0

    // 整体重赋 settings（而非改成员），与 weather 设置页同法：让组件侧依赖
    // settings.xxx 的绑定随新对象重算刷新（对话框 Ok 时由 updateSettings 持久化）
    function updateSetting(key, value) {
        var changes = {}
        changes[key] = value
        root.settings = Object.assign({}, root.settings || {}, changes)
    }

    SettingCard {
        Layout.fillWidth: true

        icon.name: "ic_fluent_text_case_title_20_regular"
        title: qsTr("Event Title")
        description: qsTr("Shown as the widget header title")

        TextField {
            id: titleField
            Layout.preferredWidth: 220
            placeholderText: qsTr("e.g. Final Exam")
            onEditingFinished: root.updateSetting("title", text)
            Component.onCompleted: {
                // 命令式初始化（对齐 settings/Text.qml 先例）：若写成 text: 绑定，
                // 选日期触发的 settings 整体重赋会重算并覆盖用户未提交的输入
                text = (root.settings && root.settings.title) ? root.settings.title : ""
            }
        }
    }

    SettingCard {
        Layout.fillWidth: true

        icon.name: "ic_fluent_calendar_20_regular"
        title: qsTr("Target Date")
        description: root.targetDateSet
            ? qsTr("Counting down to %1").arg(root.targetDateStr)
            : qsTr("Pick the day to count down to")

        CalendarDatePicker {
            id: dateField
            textFormat: "yyyy-MM-dd"

            Component.onCompleted: {
                if (root.targetDateSet) {
                    var parts = root.targetDateStr.split("-")
                    dateField.selectedDate =
                        new Date(parseInt(parts[0]), parseInt(parts[1]) - 1, parseInt(parts[2]))
                }
            }
            onDateSelected: function (date) {
                var m = date.getMonth() + 1
                var d = date.getDate()
                root.updateSetting("target_date",
                    date.getFullYear()
                    + "-" + (m < 10 ? "0" + m : m)
                    + "-" + (d < 10 ? "0" + d : d))
            }
        }
    }
}
