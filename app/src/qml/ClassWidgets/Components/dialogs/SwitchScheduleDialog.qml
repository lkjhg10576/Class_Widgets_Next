import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RinUI
import "../editor" // ScheduleClip（dialogs/ 无 qmldir，QML 隐式导入只覆盖本目录）

// 托盘菜单"切换课程表"的弹出界面（B4 托盘菜单扩展）。
// 与 TrayPanel 内嵌的课程表切换区使用同一套 ScheduleClip + load() 路径；
// 实例常驻 MainInterface（TrayPanel 经 Loader 按需创建，见 A5），因此
// 打开时重扫课表目录，保证运行期导入/新建的课表及时出现。
Dialog {
    id: switchScheduleDialog
    title: qsTr("Switch Schedule")
    standardButtons: Dialog.Close
    modal: true

    onOpened: {
        scheduleList.model = AppCentral.scheduleManager.schedules()
        scheduleList.positionViewAtBeginning()
    }

    ColumnLayout {
        spacing: 12
        Layout.fillWidth: true

        ListView {
            id: scheduleList
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 320)
            spacing: 8
            clip: true

            delegate: ScheduleClip {
                width: ListView.view ? ListView.view.width : 0
                filename: modelData.name
                selected: AppCentral.scheduleManager.currentScheduleName === modelData.name
                iconVisible: false
                actionEnabled: false
                onClicked: {
                    AppCentral.scheduleManager.load(modelData.name)
                    switchScheduleDialog.close()
                }
            }

            ScrollBar.vertical: ScrollBar { }
        }
    }
}
