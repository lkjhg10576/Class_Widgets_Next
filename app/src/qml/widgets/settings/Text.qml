 import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RinUI
import ClassWidgets.Plugins

SettingsLayout {
    SettingCard {
        Layout.fillWidth: true

        icon.name: "ic_fluent_subtitles_20_regular"
        title: qsTr("Marquee Text")
        description: qsTr("If enabled, the text will scroll from left to right, if the widget is not wide enough.")

        Switch {
            id: marqueeSwitch
            onCheckedChanged: {
                settings.marquee = marqueeSwitch.checked
            }
            Component.onCompleted: {
                marqueeSwitch.checked = settings.marquee
            }
        }
    }
    SettingCard {
        Layout.fillWidth: true

        icon.name: "ic_fluent_broad_activity_feed_20_regular"
        title: qsTr("Maximum Width")
        description: qsTr("Set the maximum width of the widget to display")

        enabled: marqueeSwitch.checked
        SpinBox {
            id: maxWidthSpinBox
            from: 100
            to: 500
            stepSize: 10
            onValueChanged: {
                settings.max_width = maxWidthSpinBox.value
            }
            Component.onCompleted: {
                maxWidthSpinBox.value = settings.max_width || 150
            }
        }
    }
    SettingCard {
        Layout.fillWidth: true

        icon.name: "ic_fluent_text_case_title_20_regular"
        title: qsTr("Custom Text")

        TextField {
            width: 300
            id: textTextField
            onEditingFinished: {
                settings.text = textTextField.text
            }
            Component.onCompleted: {
                textTextField.text = settings.text
            }
        }
    }
}