import QtQuick
import QtQuick.Window as QQW

// 全屏课程白板 / 熄屏展示（four-plugins E 二期挂载点，另排期；本计划一期仅预留）。
// 本文件为最小可用单窗双主题版：纯白/纯黑两主题，课程条全宽，expanded 详情 +
// 大字号倒计时；独立 Frameless+Tool+StaysOnTop，不进主窗口蒙版。
// 二期完整版再补：device_type/widgets_layer/countdown_style/auto_close_* 键、
// 光标 idle 4s 隐藏；三期画笔套件（Canvas/StrokeStore/pages.json/撤销栈）另立项，
// 本计划只预留 boardStrokes 落盘路径（extensions.schedule_peek.board_strokes）与工具栏挂载点。
QQW.Window {
    id: boardWindow
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool
    color: boardWindow.darkTheme ? "black" : "white"

    property bool darkTheme: false
    // 工具栏挂载点（三期画笔工具栏挂此处，boardToolsLoader）
    property alias toolsLoader: boardToolsLoader

    onClosing: function(event) {
        event.accepted = false
        WindowManager.closeLessonsBoard()
    }

    readonly property var dayEntries: AppCentral.scheduleRuntime
                                      ? AppCentral.scheduleRuntime.currentDayEntries : []

    width: 900
    height: 600

    Component.onCompleted: {
        const s = Qt.application.screens[0]
        if (s) {
            boardWindow.x = s.virtualX + (s.width - boardWindow.width) / 2
            boardWindow.y = s.virtualY + (s.height - boardWindow.height) / 2
        }
    }

    Column {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 12

        Row {
            spacing: 12
            Text {
                text: qsTr("今日课程")
                color: boardWindow.darkTheme ? "white" : "#1b1b1f"
                font.pixelSize: 28
                font.bold: true
            }
            Item { width: 12; height: 1 }
            Rectangle {
                width: 120; height: 36; radius: 8
                color: boardWindow.darkTheme ? Qt.rgba(1, 1, 1, 0.15) : Qt.rgba(0, 0, 0, 0.08)
                Text {
                    anchors.centerIn: parent
                    text: boardWindow.darkTheme ? qsTr("纯黑") : qsTr("纯白")
                    color: boardWindow.darkTheme ? "white" : "#1b1b1f"
                }
                TapHandler { onTapped: boardWindow.darkTheme = !boardWindow.darkTheme }
            }
            Rectangle {
                width: 120; height: 36; radius: 8
                color: boardWindow.darkTheme ? Qt.rgba(1, 1, 1, 0.15) : Qt.rgba(0, 0, 0, 0.08)
                Text {
                    anchors.centerIn: parent
                    text: qsTr("关闭")
                    color: boardWindow.darkTheme ? "white" : "#1b1b1f"
                }
                TapHandler { onTapped: WindowManager.closeLessonsBoard() }
            }
        }

        // 课程条固定 4,4 全宽（expanded 详情 + 大字号倒计时）
        ListView {
            width: parent.width
            height: parent.height - 80
            model: boardWindow.dayEntries
            spacing: 8
            clip: true
            delegate: Rectangle {
                required property var modelData
                required property int index
                width: ListView.view.width
                height: 64
                radius: 10
                color: boardWindow.darkTheme ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0, 0, 0, 0.05)
                Row {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 16
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: (modelData.startTime || "") + "–" + (modelData.endTime || "")
                        color: boardWindow.darkTheme ? Qt.rgba(1, 1, 1, 0.7) : Qt.rgba(0, 0, 0, 0.6)
                        font.pixelSize: 20
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.title || modelData.subjectId || ""
                        color: boardWindow.darkTheme ? "white" : "#1b1b1f"
                        font.pixelSize: 24
                        font.bold: true
                    }
                }
            }
        }

        // 三期画笔工具栏挂载点（本期空，另立项时填充 Canvas 工具条）
        Loader {
            id: boardToolsLoader
            width: parent.width
            height: 0
        }
    }
}
