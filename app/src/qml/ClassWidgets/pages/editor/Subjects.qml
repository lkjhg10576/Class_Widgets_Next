import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RinUI
import ClassWidgets.Components


Item {
    id: root
    // SaveFlyout {}
    property string editedSubjectId: ""

    function openEditDialog(subjectId, name, simplifiedName, teacher, icon, color, location, isLocalClassroom) {
        editedSubjectId = subjectId
        subjectID.text = subjectId
        subjectSimplifiedName.text = simplifiedName || ""
        subjectName.text = name || ""
        subjectTeacher.text = teacher || ""
        subjectLocation.text = location || ""
        subjectColor.color = color || "#13c4d6"
        subjectIsLocalClassroom.checked = isLocalClassroom
        iconBtn.icon.name = icon || "ic_fluent_square_hint_20_regular"
        editDialog.open()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 8

        RowLayout {
            spacing: 8
            Layout.alignment: Qt.AlignRight

            Button {
                enabled: !AppCentral.scheduleManager.isReadonly()
                icon.name: "ic_fluent_arrow_reset_20_regular"
                text: qsTr("Restore Defaults")
                onClicked: {
                    restoreConfirmDialog.open()
                }

                Dialog {
                    id: restoreConfirmDialog
                    title: qsTr("Restore Defaults")
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Are you sure you want to restore the default subjects?")
                    }
                    standardButtons: Dialog.Yes | Dialog.No
                    onAccepted: {
                        AppCentral.scheduleEditor.restoreDefaultSubjects()
                        restoreConfirmDialog.close()
                    }
                }
            }

            ToolSeparator {}

            Button {
                enabled: !AppCentral.scheduleManager.isReadonly()
                highlighted: true
                icon.name: "ic_fluent_add_20_regular"
                text: qsTr("Add Subject")
                onClicked: {
                    AppCentral.scheduleEditor.addSubject(
                        qsTr("Subject"), "", "", "#13b4d6", "", true
                    )
                }
            }
        }

        GridView {
            id: subjectsGrid
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            property int spacing: 8

            cellWidth: Math.max(200, width / Math.floor(width / 200))
            cellHeight: 175
            flow: GridView.FlowLeftToRight

            model: AppCentral.scheduleEditor.subjects

            delegate: SubjectClip {
                enabled: !AppCentral.scheduleManager.isReadonly()
                onEditRequested: function(subjectId, name, simplifiedName, teacher, icon, color, location, isLocalClassroom) {
                    root.openEditDialog(
                        subjectId, name, simplifiedName, teacher, icon, color, location, isLocalClassroom
                    )
                }
            }

            ScrollBar.vertical: ScrollBar {}
        }
    }

    Dialog {
        id: editDialog
        title: qsTr("Edit Subject")
        width: 420
        modal: true

        ColumnLayout {
            spacing: 12
            Layout.fillWidth: true

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: qsTr("ID") }
                    TextField { id: subjectID; readOnly: true; Layout.fillWidth: true }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: qsTr("Simplified Name") }
                    TextField { id: subjectSimplifiedName; Layout.fillWidth: true }
                }
            }

            RowLayout {
                Text { text: qsTr("Subject Name"); Layout.fillWidth: true }
                TextField { id: subjectName; placeholderText: qsTr("e.g. Science"); Layout.preferredWidth: 250 }
            }

            RowLayout {
                Text { text: qsTr("Teacher"); Layout.fillWidth: true }
                TextField { id: subjectTeacher; Layout.preferredWidth: 250 }
            }

            RowLayout {
                Text { text: qsTr("Location"); Layout.fillWidth: true }
                TextField { id: subjectLocation; placeholderText: qsTr("e.g. Room 7813"); Layout.preferredWidth: 250 }
            }

            RowLayout {
                Text { text: qsTr("Color"); Layout.fillWidth: true }
                DropDownColorPicker {
                    id: subjectColor
                    position: Position.Left
                    textVisible: true
                    hexText: true
                }
            }

            RowLayout {
                Text { text: qsTr("Held in homeroom"); Layout.fillWidth: true }
                Button {
                    id: explainButton
                    icon.name: "ic_fluent_question_circle_20_regular"
                    implicitWidth: 24
                    implicitHeight: 24
                    onClicked: explainFlyout.open()
                }
                Switch { id: subjectIsLocalClassroom }
            }

            RowLayout {
                Text { text: qsTr("Icon"); Layout.fillWidth: true }
                DropDownButton {
                    id: iconBtn
                    icon.name: "ic_fluent_square_hint_20_regular"
                    onClicked: iconPicker.open()

                    IconPicker {
                        id: iconPicker
                        parent: iconBtn
                        position: Position.Top
                        onIconPicked: function(name) { iconBtn.icon.name = name }
                    }
                }
            }
        }

        Flyout {
            id: explainFlyout
            parent: explainButton
            width: 300
            text: qsTr(
                "Enable if the subject is taught in your homeroom classroom.  \n" +
                "If it takes place in another location, such as a sport field, lab, or another classroom, leave it off."
            )
        }

        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: AppCentral.scheduleEditor.updateSubject(
            editedSubjectId, subjectName.text, subjectSimplifiedName.text, subjectTeacher.text,
            iconBtn.icon.name, subjectColor.color.toString(), subjectLocation.text,
            subjectIsLocalClassroom.checked
        )
    }
}
