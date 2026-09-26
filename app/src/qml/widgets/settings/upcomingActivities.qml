 import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RinUI
import ClassWidgets.Plugins

SettingsLayout {
    SettingCard {
        Layout.fillWidth: true

        icon.name: "ic_fluent_broad_activity_feed_20_regular"
        title: qsTr("Max number of activities")
        description: qsTr("Set the maximum number of activities to display in the upcoming activities view")

        SpinBox {
            id: maxActivitiesSpinBox
            from: 1
            to: 7
            onValueChanged: {
                settings.max_activities = maxActivitiesSpinBox.value
            }
            Component.onCompleted: {
                const saved = settings.max_activities
                maxActivitiesSpinBox.value = (saved >= 1 && saved <= 7) ? saved : 7
            }
        }
    }
    SettingCard {
        Layout.fillWidth: true

        icon.name: "ic_fluent_text_case_title_20_regular"
        title: qsTr("Show abbreviation")
        description: qsTr("Use the abbreviation set for each subject, or its first character when unset")

        Switch {
            id: showAbbreviationSwitch
            onCheckedChanged: {
                // 存量键 full_name 语义与开关相反：true = 显示全称 = 不缩写
                settings.full_name = !showAbbreviationSwitch.checked
            }
            Component.onCompleted: {
                showAbbreviationSwitch.checked = !(settings.full_name === true)
            }
        }
    }
}
