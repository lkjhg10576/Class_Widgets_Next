import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQml.Models
import RinUI


ColumnLayout {
    id: root
    property real panelWidth: 375
    property bool editMode: false
    signal shortcutTriggered()
    spacing: 8

    Component.onCompleted: {
        if (UtilsBackend.shortcuts.length === 0) {
            editMode = true
        }
    }

    RowLayout {
        Layout.fillWidth: true

        Text {
            typography: Typography.BodyStrong
            text: qsTr("Shortcuts")
        }

        Item { Layout.fillWidth: true }

        ToolButton {
            visible: root.editMode
            enabled: UtilsBackend.availableShortcuts.length > 0
            flat: true
            icon.name: "ic_fluent_add_20_regular"
            onClicked: addShortcutDialog.open()

            ToolTip { text: qsTr("Add"); visible: parent.hovered }
        }

        ToolButton {
            flat: true
            icon.name: root.editMode
                ? "ic_fluent_checkmark_20_regular"
                : "ic_fluent_edit_20_regular"
            onClicked: root.editMode = !root.editMode

            ToolTip { text: root.editMode ? qsTr("Done") : qsTr("Edit"); visible: parent.hovered }
        }
        ToolButton {
            flat: true
            icon.name: "ic_fluent_chevron_right_20_regular"
            visible: !root.editMode
            enabled: UtilsBackend.shortcuts.length > 0
            onClicked: allShortcutsDialog.open()
            ToolTip { text: qsTr("All Shortcuts"); visible: parent.hovered }
        }
    }

    EmptyState {
        Layout.fillWidth: true
        Layout.fillHeight: true
        icon.name: "ic_fluent_uninstall_app_20_regular"
        title: qsTr("No shortcuts yet")
        description: qsTr("Click \"+\" to add shortcuts.")
        visible: visualShortcutModel.count === 0
    }

    GridView {
        id: shortcutGrid
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredHeight: Math.min(contentHeight, 244)
        // Layout.maximumHeight: 244
        clip: true
        interactive: contentHeight > height
        cellWidth: width / 3
        cellHeight: 84
        // columns: Math.floor(width / (96 + 6))
        // rowSpacing: 12
        // columnSpacing: 16
        model: visualShortcutModel

        delegate: Item {
            id: shortcutTile
            required property var modelData
            property int visualIndex: DelegateModel.itemsIndex
            width: shortcutGrid.cellWidth - 6
            height: shortcutGrid.cellHeight - 6

            Item {
                id: shortcutContent
                width: shortcutTile.width
                height: shortcutTile.height
                z: shortcutDrag.drag.active ? 1 : 0
                Drag.active: shortcutDrag.drag.active
                Drag.source: shortcutTile
                Drag.hotSpot.x: width / 2
                Drag.hotSpot.y: height / 2
                clip: false

                states: State {
                    when: shortcutDrag.drag.active
                    ParentChange {
                        target: shortcutContent
                        parent: shortcutGrid.contentItem
                    }
                }

                ColumnLayout {
                    anchors.fill: parent
                    Clip {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 6

                        Icon {
                            anchors.centerIn: parent
                            name: modelData.iconIsSource ? "" : modelData.icon ? modelData.icon : "ic_fluent_open_20_regular"
                            source: modelData.iconIsSource ? modelData.icon : ""
                            size: 22
                        }

                        onClicked: {
                            if (!root.editMode && UtilsBackend.executeShortcut(modelData.id)) {
                                root.shortcutTriggered()
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        typography: Typography.Caption
                        text: modelData.name
                        // maximumLineCount: 1
                        elide: Text.ElideRight
                    }
                }

                MouseArea {
                    id: shortcutDrag
                    anchors.fill: parent
                    enabled: root.editMode && !Configs.isKeyLocked("preferences.shortcuts")
                    drag.target: shortcutContent

                    onReleased: UtilsBackend.moveShortcutTo(
                                    modelData.id, shortcutTile.visualIndex)
                }

                // ToolButton {
                //     id: deleteBtn
                //     visible: widgetsContainer.editMode
                //     icon.name: "ic_fluent_line_horizontal_1_20_filled"
                //     size: 12
                //     width: 24
                //     height: 24
                //     anchors.top: parent.top
                //     anchors.left: parent.left
                //     onClicked: WidgetsModel.removeInstance(model.instanceId)
                // }

                ToolButton {
                    visible: root.editMode
                    anchors.top: parent.top
                    anchors.left: parent.left
                    size: 12
                    width: 24
                    height: 24
                    // anchors.margins: -6
                    icon.name: "ic_fluent_line_horizontal_1_20_filled"
                    onClicked: UtilsBackend.setShortcutEnabled(modelData.id, false)

                    ToolTip { text: qsTr("Remove"); visible: parent.hovered }
                }
            }

            DropArea {
                anchors.fill: parent
                onEntered: function(drag) {
                    if (drag.source && drag.source !== shortcutTile
                            && drag.source.visualIndex !== shortcutTile.visualIndex) {
                        visualShortcutModel.items.move(
                                    drag.source.visualIndex, shortcutTile.visualIndex)
                    }
                }
            }
        }

        move: Transition {
            NumberAnimation { properties: "x,y"; duration: 160; easing.type: Easing.OutCubic }
        }

        moveDisplaced: Transition {
            NumberAnimation { properties: "x,y"; duration: 160; easing.type: Easing.OutCubic }
        }

        ScrollBar.vertical: ScrollBar { }
    }

    DelegateModel {
        id: visualShortcutModel
        model: UtilsBackend.shortcuts
    }

    Dialog {
        id: allShortcutsDialog
        title: qsTr("All Shortcuts")
        modal: true
        width: Math.min(root.panelWidth - 28, 420)
        height: 460
        standardButtons: Dialog.Close

        GridView {
            id: allShortcutsGrid
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            cellWidth: allShortcutsGrid.width / 3
            cellHeight: 84
            model: UtilsBackend.allShortcuts

            delegate: Item {
                width: allShortcutsGrid.cellWidth - 6
                height: allShortcutsGrid.cellHeight - 6

                ColumnLayout {
                    anchors.fill: parent
                    Clip {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 6

                        Icon {
                            anchors.centerIn: parent
                            name: modelData.iconIsSource ? "" : modelData.icon ? modelData.icon : "ic_fluent_open_20_regular"
                            source: modelData.iconIsSource ? modelData.icon : ""
                            size: 22
                        }

                        onClicked: {
                            if (UtilsBackend.executeShortcut(modelData.id)) {
                                allShortcutsDialog.close()
                                root.shortcutTriggered()
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        typography: Typography.Caption
                        text: modelData.name
                        elide: Text.ElideRight
                    }
                }
            }

            ScrollBar.vertical: ScrollBar { }
        }
    }

    Dialog {
        id: addShortcutDialog
        signal addRequested()
        title: qsTr("Add Shortcuts")
        modal: true
        width: Math.min(root.panelWidth - 28, 420)
        height: 460
        standardButtons: Dialog.Close

        ListView {
            id: shortcutsList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 8
            model: UtilsBackend.availableShortcuts

            onModelChanged: {
                if (model.length !== 0) {
                    return
                }
                addShortcutDialog.close()
            }

            delegate: Clip {
                width: shortcutsList.width
                height: 60
                radius: 6

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8

                    Icon {
                        name: modelData.iconIsSource ? "" : modelData.icon
                        source: modelData.iconIsSource ? modelData.icon : ""
                        size: 20
                    }

                    Text {
                        Layout.fillWidth: true
                        text: modelData.name
                        elide: Text.ElideRight
                    }

                    ToolButton {
                        flat: true
                        icon.name: "ic_fluent_add_20_regular"
                        enabled: !Configs.isKeyLocked("preferences.shortcuts")
                        onClicked: {
                            UtilsBackend.setShortcutEnabled(modelData.id, true)
                        }

                        ToolTip { text: qsTr("Add"); visible: parent.hovered }
                    }
                }
            }
        }
    }
}
