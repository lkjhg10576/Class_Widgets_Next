import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RinUI
import "../../Components/tutorial" as TutorialComponents

TutorialComponents.TutorialPage {
    id: root

    property var tutorial
    title: qsTr("Choose widget interactions")
    description: qsTr("Decide how widgets get out of your way while you work.")
    currentStep: 4
    totalSteps: 6
    icon.source: PathManager.images("icons/cwn_settings.png")

    function hidePreviewSource(name) {
        return PathManager.images("tutorial/" + name + (Theme.isDark() ? "-dark.png" : "-light.png"))
    }

    ColumnLayout {
        width: parent.width
        spacing: 4

        SettingExpander {
            Layout.fillWidth: true
            Layout.minimumHeight: 200
            icon.name: "ic_fluent_tap_single_20_regular"
            title: qsTr("Tap Action")
            description: qsTr("Choose whether tapping a widget hides it, switches to mini mode, or opens a floating widget")
            expanded: true
            enabled: !hoverFadeSwitch.checked

            action: Switch {
                enabled: !Configs.isKeyLocked("interactions.hide.clicked")
                onCheckedChanged: Configs.set("interactions.hide.clicked", checked)
                Component.onCompleted: checked = Configs.data.interactions.hide.clicked
            }

            ButtonGroup {
                id: hideModeGroup
            }

            SettingItem {
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    Repeater {
                        model: [
                            {
                                "name": qsTr("Hide"),
                                "value": "hide",
                                "preview": "hide_default"
                            },
                            {
                                "name": qsTr("Mini Mode"),
                                "value": "mini_mode",
                                "preview": "hide_mini"
                            },
                            {
                                "name": qsTr("Floating Widget"),
                                "value": "floating_widget",
                                "preview": "hide_floating"
                            }
                        ]

                        delegate: ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            Image {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 72
                                source: root.hidePreviewSource(modelData.preview)
                                // 不加 sourceSize：本图（321×225，非大图）宽度来自
                                // fillWidth 布局，而布局又受图片隐式尺寸影响；若
                                // sourceSize 绑定 width，解码结果改变隐式尺寸会反过来
                                // 改变 width，触发无限"重解码-重排"振荡（事件循环饿死，
                                // 整个引导流程卡死）。A7 跳过规则（非大图）同样适用。
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true
                            }

                            RadioButton {
                                Layout.alignment: Qt.AlignHCenter
                                text: modelData.name
                                checked: Configs.data.interactions.tapped_action === modelData.value
                                enabled: !Configs.isKeyLocked("interactions.tapped_action")
                                ButtonGroup.group: hideModeGroup
                                onClicked: Configs.set("interactions.tapped_action", modelData.value)
                            }
                        }
                    }
                }
            }
        }

        SettingCard {
            Layout.fillWidth: true
            icon.name: "ic_fluent_cursor_20_regular"
            title: qsTr("Hover Fade")
            description: qsTr("Hover to make widgets transparent and let clicks pass through")

            Switch {
                id: hoverFadeSwitch
                enabled: !Configs.isKeyLocked("interactions.hover_fade")
                onCheckedChanged: Configs.set("interactions.hover_fade", checked)
                Component.onCompleted: checked = Configs.data.interactions.hover_fade
            }
        }

        SettingExpander {
            Layout.fillWidth: true
            icon.name: "ic_fluent_slide_hide_20_regular"
            title: qsTr("More hide behavior")
            description: qsTr("Choose whether widgets hide, switch to Mini Mode, or open a floating widget when triggered")

            action: ComboBox {
                Layout.preferredWidth: 180
                model: ListModel {
                    ListElement { text: qsTr("Hide Widgets"); value: "hide" }
                    ListElement { text: qsTr("Switch to mini mode"); value: "mini_mode" }
                    ListElement { text: qsTr("Floating widget"); value: "floating_widget" }
                }
                textRole: "text"
                valueRole: "value"
                enabled: !Configs.isKeyLocked("interactions.hide.action")
                onCurrentValueChanged: if (focus) Configs.set("interactions.hide.action", currentValue)
                Component.onCompleted: currentIndex = indexOfValue(Configs.data.interactions.hide.action)
            }
            SettingItem {
                ColumnLayout {
                    Layout.fillWidth: true
                    CheckBox {
                        Layout.fillWidth: true
                        text: qsTr("Hide when in class")
                        enabled: !Configs.isKeyLocked("interactions.hide.in_class")
                        onCheckedChanged: Configs.set("interactions.hide.in_class", checked)
                        Component.onCompleted: checked = Configs.data.interactions.hide.in_class
                    }
                    CheckBox {
                        Layout.fillWidth: true
                        text: qsTr("Hide when a window is maximized")
                        enabled: !Configs.isKeyLocked("interactions.hide.maximized") && Qt.platform.os === "windows"
                        onCheckedChanged: Configs.set("interactions.hide.maximized", checked)
                        Component.onCompleted: checked = Configs.data.interactions.hide.maximized
                    }
                    CheckBox {
                        Layout.fillWidth: true
                        text: qsTr("Hide when a window enters fullscreen")
                        enabled: !Configs.isKeyLocked("interactions.hide.fullscreen") && Qt.platform.os === "windows"
                        onCheckedChanged: Configs.set("interactions.hide.fullscreen", checked)
                        Component.onCompleted: checked = Configs.data.interactions.hide.fullscreen
                    }
                }
            }
        }
    }
}
