import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RinUI

ColumnLayout {
    Layout.fillWidth: true
    Text {
        typography: Typography.BodyStrong
        text: qsTr("Overview")
    }

    SettingExpander {
        Layout.fillWidth: true
        icon.name: "ic_fluent_alert_badge_20_regular"
        title: qsTr("Notifications")
        description: qsTr("Debug")

        SettingItem {
            title: qsTr("Send notifications")
            // 此 SettingItem 没有描述
            ColumnLayout {
                Layout.fillWidth: true
                ComboBox {
                    id: notificationLevel
                    Layout.fillWidth: true
                    model: [qsTr("Info"), qsTr("Announcement"), qsTr("Warning"), qsTr("System")]
                }
                TextField {
                    id: notificationTitle
                    Layout.fillWidth: true
                    placeholderText: qsTr("Title")
                }
                TextField {
                    id: notificationText
                    Layout.fillWidth: true
                    placeholderText: qsTr("Text")
                }

                Button {
                    highlighted: true
                    text: qsTr("Send")
                    onClicked: {
                        let provider = UtilsBackend.debugNotificationProvider
                        if (provider) {
                            provider.push(
                                notificationLevel.currentIndex,                  // level, 对应 NotificationLevel.ANNOUNCEMENT
                                notificationTitle.text || qsTr("Debug"), // title
                                notificationText.text || qsTr("Debug message"),    // message
                                4000,
                                true
                            )
                        }
                    }
                    // onClicked: {
                    //     AppCentral.notification.push(
                    //         "ic_fluent_alert_20_regular",  // icon
                    //         notificationLevel.currentIndex,  // level
                    //         notificationTitle.text,  // title
                    //         notificationText.text  // text
                    //     )
                    // }
                }
            }
        }
    }

    SettingExpander {
        Layout.fillWidth: true
        icon.name: "ic_fluent_info_20_regular"
        title: qsTr("Overview")
        description: "Class Widgets Next | " + AppCentral.globalConfig.app.version

        SettingItem {
            title: qsTr("Version")
            // 此 SettingItem 没有描述
            Text {
                text: AppCentral.globalConfig.app.version
            }
        }
    }

    SettingCard {
        Layout.fillWidth: true
        title: qsTr("App Theme")
        description: qsTr("Select which app theme to display")
        icon.name: "ic_fluent_paint_brush_20_regular"

        ComboBox {
            property var data: [Theme.mode.Light, Theme.mode.Dark, Theme.mode.Auto]
            model: ListModel {
                ListElement { text: qsTr("Light") }
                ListElement { text: qsTr("Dark") }
                ListElement { text: qsTr("Use system setting") }
            }
            currentIndex: data.indexOf(Theme.getTheme())
            onCurrentIndexChanged: {
                Theme.setTheme(data[currentIndex])
            }
        }
    }

    // SettingCard {
    //     Layout.fillWidth: true
    //     icon.name: "ic_fluent_document_bullet_list_off_20_regular"
    //     title: qsTr("No Logs")
    //     description: qsTr("Do not save logs to local storage.")
    //
    //     // Control placed on the right side via the 'content' default property
    //     Switch { // This Switch is assigned to the 'content' property
    //         checked: true
    //     }
    // }
}