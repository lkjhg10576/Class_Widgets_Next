import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects
import ClassWidgets.Components
import RinUI

Clip {
    id: root

    property var plugin: ({})
    property bool selected: true
    property bool installed: false
    property bool installActive: false
    property string installStatus: "Idle"
    property real installProgress: 0
    property int installDownloadedBytes: 0
    property int installTotalBytes: 0
    property string baseUrl: ""
    property bool iconLoadFailed: false

    signal selectionToggled(bool selected)

    radius: 8
    implicitHeight: 82

    function resourceUrl(pluginId, resource) {
        return pluginId && root.baseUrl
               ? root.baseUrl + "/api/plugins/" + encodeURIComponent(pluginId) + "/resources/" + resource
               : ""
    }

    function iconSource() {
        if (root.iconLoadFailed || root.resourceUrl(root.pluginId, "icon") === "")
            return PathManager.images("default_plugin.png")
        return root.resourceUrl(root.pluginId, "icon")
    }

    readonly property string pluginId: plugin && plugin.id ? plugin.id : ""
    readonly property string pluginName: plugin && plugin.name ? plugin.name : qsTr("Unknown plugin")
    readonly property string pluginDescription: plugin
        ? (plugin.description || plugin.desc || plugin.summary || "") : ""
    readonly property bool official: !!(plugin && (plugin.official || plugin.verified
                                                   || plugin.certified || plugin.is_official))
    readonly property bool featured: !!(plugin && (plugin.featured || plugin.recommended
                                                   || plugin.is_featured))
    readonly property bool busy: installActive && (installStatus === "Downloading"
                                                     || installStatus === "Paused"
                                                     || installStatus === "Installing")
    readonly property bool determinateProgress: installStatus === "Downloading"
                                             || installStatus === "Paused"

    onPluginChanged: root.iconLoadFailed = false

    background: Frame {
        radius: root.radius
        // color: root.down ? Colors.proxy.controlPressedColor
        //      : root.hovered ? Qt.alpha(Colors.proxy.controlFillColor, 0.6)
        //      : Colors.proxy.controlFillColor
        // border.width: 1
        border.color: root.selected ? root.installed && !root.busy ? Colors.proxy.subtleSecondaryColor : Colors.proxy.primaryColor
                    : Colors.proxy.controlBorderColor
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10

        CheckBox {
            Layout.alignment: Qt.AlignVCenter
            checked: root.selected
            enabled: !root.installed && !root.busy
            // text: root.installed && !root.busy ? qsTr("Installed") : ""
            onClicked: root.selectionToggled(checked)
        }

        Rectangle {
            id: iconFrame
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: 48
            Layout.preferredHeight: 48
            radius: 16
            color: Colors.proxy.backgroundColor
            border.color: Colors.proxy.controlBorderColor
            border.width: 1
            clip: true

            Skeleton {
                anchors.fill: parent
                radius: iconFrame.radius
                running: !pluginIcon.iconLoaded && !root.iconLoadFailed
                visible: !pluginIcon.iconLoaded && !root.iconLoadFailed

                layer.enabled: visible
                layer.effect: OpacityMask {
                    maskSource: Rectangle {
                        width: iconFrame.width
                        height: iconFrame.height
                        radius: iconFrame.radius
                    }
                }
            }

            Image {
                id: pluginIcon
                anchors.fill: parent
                source: root.iconSource()
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: true
                visible: pluginIcon.iconLoaded

                property bool iconLoaded: false

                onSourceChanged: iconLoaded = false

                onStatusChanged: {
                    if (status === Image.Ready)
                        iconLoaded = true
                    else if (status === Image.Error && !root.iconLoadFailed)
                        root.iconLoadFailed = true
                }

                layer.enabled: true
                layer.effect: OpacityMask {
                    maskSource: Rectangle {
                        width: iconFrame.width
                        height: iconFrame.height
                        radius: 16
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.alignment: Qt.AlignVCenter
            spacing: 3

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                Text {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: root.pluginName
                    typography: Typography.BodyStrong
                    elide: Text.ElideRight
                    maximumLineCount: 1
                }

                // Rectangle {
                //     visible: root.official
                //     Layout.preferredHeight: 20
                //     Layout.preferredWidth: officialText.implicitWidth + 14
                //     radius: 5
                //     color: Qt.alpha(Colors.proxy.primaryColor, 0.14)
                //
                //     Text {
                //         id: officialText
                //         anchors.centerIn: parent
                //         text: qsTr("Official")
                //         typography: Typography.Caption
                //         color: Colors.proxy.primaryColor
                //     }
                // }

            }

            Text {
                Layout.fillWidth: true
                text: root.pluginDescription
                typography: Typography.Caption
                elide: Text.ElideRight
                maximumLineCount: 1
            }

        }

        ProgressRing {
            Layout.alignment: Qt.AlignVCenter
            visible: root.busy
            size: 22
            strokeWidth: 2
            value: root.determinateProgress ? root.installProgress / 100 : 0
            indeterminate: !root.determinateProgress
        }

    }
}
