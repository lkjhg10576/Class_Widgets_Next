import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import RinUI
import Debugger


ColumnLayout {
    Layout.fillWidth: true
    //
    // EditSchedule {
    //     id: editScheduleWindow
    // }

    Text {
        typography: Typography.BodyStrong
        text: "Dashboard"
    }

    Frame {
        Layout.fillWidth: true
        ColumnLayout {
            anchors.fill: parent
            Layout.topMargin: 12
            Layout.bottomMargin: 12
            Text {
                text: "Logs"
                typography: Typography.BodyStrong
            }
            // 过滤栏：搜索框 + 级别下拉
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                TextField {
                    id: logSearchField
                    Layout.fillWidth: true
                    placeholderText: qsTr("Search logs...")
                    onTextEdited: UtilsBackend.setLogFilterText(text)
                }

                ComboBox {
                    id: logLevelFilter
                    Layout.preferredWidth: 150
                    textRole: "text"
                    valueRole: "value"
                    model: ListModel {
                        ListElement { text: "All Levels"; value: "" }
                        ListElement { text: "DEBUG"; value: "DEBUG" }
                        ListElement { text: "INFO"; value: "INFO" }
                        ListElement { text: "WARNING"; value: "WARNING" }
                        ListElement { text: "ERROR"; value: "ERROR" }
                        ListElement { text: "SUCCESS"; value: "SUCCESS" }
                    }
                    onActivated: {
                        UtilsBackend.setLogFilterLevel(model.get(currentIndex).value)
                    }
                }
            }
            // 日志列表（含“回到最新日志”悬浮按钮）
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 300

                ListView {
                    id: logsList
                    anchors.fill: parent
                    clip: true
                    model: UtilsBackend.logs
                    spacing: 0


                    property bool atBottom: true
                    property bool initialized: false

                    onCountChanged: {
                        if (!initialized && count > 0) {
                            initialized = true
                            Qt.callLater(function() {
                                positionViewAtEnd()
                            })
                            return
                        }

                        if (atBottom)
                            Qt.callLater(positionViewAtEnd)
                    }

                    Component.onCompleted: {
                        positionViewAtEnd()
                    }

                    onMovementEnded: {
                        atBottom = contentY + height >= contentHeight - 2
                    }

                    delegate: Frame {
                        height: 40
                        width: logsList.width
                        HoverHandler { id: logHoverHandler }
                        frameless: !logHoverHandler.hovered
                        leftPadding: 12
                        padding: 4

                        RowLayout {
                            width: parent.width
                            spacing: 10
                            Text {
                                Layout.preferredWidth: 90
                                text: model.time
                                color: Colors.proxy.textSecondaryColor
                                elide: Text.ElideRight
                                wrapMode: Text.NoWrap
                            }
                            Text {
                                Layout.preferredWidth: 80
                                text: model.level
                                elide: Text.ElideRight
                                wrapMode: Text.NoWrap
                                color: {
                                    switch (model.level) {
                                        case "DEBUG": return Colors.proxy.systemNeutralColor
                                        case "INFO": return Colors.proxy.textColor
                                        case "WARNING": return Colors.proxy.systemCautionColor
                                        case "ERROR": return Colors.proxy.systemCriticalColor
                                        case "SUCCESS": return Colors.proxy.systemSuccessColor
                                        default: return Colors.proxy.textColor
                                    }
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: model.message
                                elide: Text.ElideRight
                                wrapMode: Text.NoWrap
                                color: {
                                    switch (model.level) {
                                        case "DEBUG": return Colors.proxy.systemNeutralColor
                                        case "INFO": return Colors.proxy.textColor
                                        case "WARNING": return Colors.proxy.systemCautionColor
                                        case "ERROR": return Colors.proxy.systemCriticalColor
                                        case "SUCCESS": return Colors.proxy.systemSuccessColor
                                        default: return Colors.proxy.textColor
                                    }
                                }
                                ToolTip {
                                    delay: 300
                                    text: model.message
                                    visible: logHoverHandler.hovered
                                }
                            }
                            ToolButton {
                                flat: true
                                onClicked: {
                                    if (UtilsBackend.copyToClipboard(JSON.stringify({
                                        time: model.time,
                                        level: model.level,
                                        message: model.message
                                    }))) {
                                        floatLayer.createInfoBar({
                                            severity: Severity.Success,
                                            text: "Copied to clipboard!",
                                        })
                                    }
                                }
                                icon.name: "ic_fluent_copy_20_regular"
                                size: 18
                            }
                        }
                    }
                }

                // 空状态占位：过滤无结果时显示
                Text {
                    anchors.centerIn: parent
                    visible: logsList.count === 0
                    text: qsTr("No logs match filter")
                    color: Colors.proxy.textSecondaryColor
                }

                // 回到最新日志的悬浮按钮（仅在离开底部时显示）
                Item {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.margins: 10
                    width: 42
                    height: 42
                    opacity: logsList.atBottom ? 0 : 1
                    visible: opacity > 0

                    RectangularShadow {
                        anchors.fill: parent
                        offset.y: 3
                        radius: width / 2
                        blur: 10
                        color: Qt.rgba(0, 0, 0, 0.15)
                    }

                    Clip {
                        anchors.fill: parent
                        radius: height / 2
                        AcrylicBrush {
                            sourceItem: logsList
                        }
                        onClicked: {
                            logsList.atBottom = true
                            logsList.positionViewAtEnd()
                        }
                        Icon {
                            name: "ic_fluent_arrow_down_20_regular"
                            anchors.centerIn: parent
                        }
                        // icon.name: "ic_fluent_arrow_down_20_regular"
                    }

                    Behavior on opacity { NumberAnimation { duration: 150; easing.type: Easing.OutQuart } }
                }
            }

        }
    }

    Expander {
        text: "Runtime Variables"
        Layout.fillWidth: true
        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 12

            // Footer
            RowLayout {
                Layout.fillWidth: true
                Item {
                    Layout.fillWidth: true
                }
                // edit
                // Button {
                //     text: "Edit Schedule"
                //     // onClicked: DebuggerCentral.showEditor()
                //     onClicked: editScheduleWindow.show()
                // }
                // reload
                Button {
                    text: "Reload Schedule File"
                    onClicked: AppCentral.scheduleManager.reload()
                }
            }

            // ScheduleRuntime
            Text {
                typography: Typography.BodyStrong
                text: "ScheduleRuntime"
            }
            VarStatus {
                Layout.fillWidth: true
                columns: 3
                Layout.preferredHeight: 350
                model: [
                    { name: "currentTime", value: AppCentral.scheduleRuntime.currentTime },
                    {
                        name: "currentDate",
                        value: JSON.stringify(AppCentral.scheduleRuntime.currentDate)   // 显示字典
                    },
                    { name: "currentDayOfWeek", value: AppCentral.scheduleRuntime.currentDayOfWeek },
                    { name: "currentWeek", value: AppCentral.scheduleRuntime.currentWeek },
                    { name: "currentWeekOfCycle", value: AppCentral.scheduleRuntime.currentWeekOfCycle },
                    { name: "scheduleMeta", value: JSON.stringify(AppCentral.scheduleRuntime.scheduleMeta) },
                    { name: "currentDayEntries", value: JSON.stringify(AppCentral.scheduleRuntime.currentDayEntries) },
                    { name: "currentEntry", value: JSON.stringify(AppCentral.scheduleRuntime.currentEntry) },  // 显示字典
                    { name: "nextEntries", value: JSON.stringify(AppCentral.scheduleRuntime.nextEntries) },
                    { name: "remainingTime", value: JSON.stringify(AppCentral.scheduleRuntime.remainingTime) },  // 显示字典
                    { name: "currentStatus", value: AppCentral.scheduleRuntime.currentStatus },
                    { name: "currentSubject", value: JSON.stringify(AppCentral.scheduleRuntime.currentSubject) },
                    { name: "currentTitle", value: AppCentral.scheduleRuntime.currentTitle }
                ]
            }
        }
    }
}