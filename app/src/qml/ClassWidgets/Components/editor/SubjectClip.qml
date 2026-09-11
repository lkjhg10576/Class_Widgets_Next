import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RinUI
import ClassWidgets.Components

Clip {
    id: subjectClip
    width: subjectsGrid.cellWidth - 8
    height: subjectsGrid.cellHeight - 6
    signal editRequested(string subjectId, string name, string simplifiedName, string teacher,
                         string icon, string color, string location, bool isLocalClassroom)
    onClicked: editRequested(
        subjectId, subjectNameText, subjectSimplifiedNameText, subjectTeacherText,
        subjectIcon, subjectColorText, subjectLocationText, subjectIsLocal
    )

    property string subjectId: modelData.id
    property string subjectIcon: modelData.icon || ""
    property string subjectSimplifiedNameText: modelData.simplifiedName || ""
    property string subjectNameText: modelData.name
    property string subjectTeacherText: modelData.teacher || ""
    property string subjectLocationText: modelData.location || ""
    property string subjectColorText: modelData.color || ""
    property bool subjectIsLocal: modelData.isLocalClassroom !== false

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        anchors.bottomMargin: 18

        ColumnLayout {
            id: subjectNameLayout
            // Layout.alignment: Qt.AlignTop | Qt.AlignLeft
            Layout.fillWidth: true
            spacing: 6

            RowLayout {
                spacing: 6
                Rectangle {
                    // circle
                    width: 26
                    height: 26
                    radius: width / 2
                    color: Qt.alpha(subjectColorText ||"#197", 0.2)  // 学科自定色

                    Icon {  //subject icon
                        anchors.centerIn: parent
                        size: 18; name: subjectIcon || "ic_fluent_hexagon_three_20_regular";
                    }
                }
                // 非本班课程
                Rectangle {
                    // circle
                    visible: !subjectIsLocal
                    width: 26
                    height: 26
                    radius: width / 2
                    color: Colors.proxy.systemCautionBackgroundColor

                    Icon {  //subject icon
                        anchors.centerIn: parent
                        color: Colors.proxy.systemCautionColor
                        size: 18; name: "ic_fluent_sign_out_20_filled";
                    }
                }
                Item { Layout.fillWidth: true }

                ToolButton {
                    Layout.preferredWidth: 32; Layout.preferredHeight: 32;
                    flat: true
                    icon.name: "ic_fluent_delete_20_regular"
                    onClicked: AppCentral.scheduleEditor.removeSubject(subjectId)
                }
            }

            Item { Layout.fillHeight: true }

            // 详细信息
            ColumnLayout {
                Layout.fillWidth: true

                Repeater {
                    model: [subjectTeacherText, subjectLocationText]

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        visible: modelData
                        Text {
                            opacity: 0.5
                            text: index === 0 ? qsTr("Teacher: ") : qsTr("Location: ")
                            elide: Text.ElideRight
                            maximumLineCount: 1
                        }
                        Text {
                            id: subjectInfo
                            Layout.fillWidth: true
                            opacity: 0.75
                            horizontalAlignment: Text.AlignRight
                            text: modelData
                            elide: Text.ElideRight
                            maximumLineCount: 1
                        }
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                typography: Typography.BodyLarge
                text: subjectNameText
                elide: Text.ElideRight
                maximumLineCount: 1
            }
        }
    }

}
