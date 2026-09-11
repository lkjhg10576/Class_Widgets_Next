import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RinUI
import QtQuick.Window as QQW

QQW.Window {
    id: classSwapRestoreDialogWindow
    visible: true
    width: screen.width
    height: screen.height
    color: "transparent"

    flags: Qt.WindowStaysOnTopHint | Qt.FramelessWindowHint | Qt.WA_TranslucentBackground

    onClosing: function(event) {
        event.accepted = false
    }

    Dialog {
        id: classSwapRestoreDialog
        width: Screen.width * 0.25
        title: qsTr("Temporary schedule detected")
        modal: true
        closePolicy: Popup.NoAutoClose

        RowLayout {
            spacing: 12
            Layout.fillWidth: true

            Icon {
                Layout.alignment: Qt.AlignTop
                size: 42
                source: PathManager.images("icons/cwn_info.png")
            }

            Text {
                Layout.fillWidth: true
                text: qsTr(
                    "Class Widgets found temporary class swaps for today.\n\n"
                    + "Do you want to continue using them, "
                    + "or discard and restore the original schedule?"
                )
                wrapMode: Text.WordWrap
            }
        }

        footer: DialogButtonBox {
            Layout.fillWidth: true

            Button {
                Layout.preferredWidth: parent.width * 0.5
                text: qsTr("Discard")
                highlighted: true
                onClicked: {
                    WindowManager.classSwapRestoreDiscard()
                }
            }

            Button {
                Layout.preferredWidth: parent.width * 0.5
                text: qsTr("Continue")
                onClicked: {
                    WindowManager.classSwapRestoreContinue()
                }
            }
        }
    }

    Component.onCompleted: {
        classSwapRestoreDialog.open()
    }
}
