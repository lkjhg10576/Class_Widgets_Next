import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RinUI
import Qt5Compat.GraphicalEffects
import ClassWidgets.Theme 1.0 as WidgetTheme

Item {
    id: root

    readonly property real widgetScale: Configs.data.preferences.scale_factor
    readonly property int previewWidth: 620
    readonly property int previewHeight: 216

    Item {
        id: scaledPreview
        width: widgetsLayout.implicitWidth
        height: widgetsLayout.implicitHeight
        anchors.centerIn: parent
        scale: root.widgetScale
        transformOrigin: Item.Center

        Behavior on scale {
            NumberAnimation { duration: 120; easing.type: Easing.OutCubic }
        }

        ColumnLayout {
            id: widgetsLayout
            anchors.fill: parent
            spacing: -12

            // Fixed-data version of widgets/eventCountdown.qml.
            Item {
                Layout.alignment: Qt.AlignCenter
                implicitWidth: countdownPreview.width
                implicitHeight: countdownPreview.height + 36

                Item {
                    width: countdownPreview.width
                    height: 36
                    anchors.top: countdownPreview.bottom
                    anchors.horizontalCenter: parent.horizontalCenter
                    clip: true

                    Rectangle {
                        id: countdownShadowSource
                        width: countdownPreview.width
                        height: countdownPreview.height
                        y: -countdownPreview.height
                        radius: Math.min(width, height, countdownPreview.cornerRadius)
                        color: "#29000000"
                        visible: false
                    }

                    DropShadow {
                        x: countdownShadowSource.x
                        y: countdownShadowSource.y
                        width: countdownShadowSource.width
                        height: countdownShadowSource.height
                        source: countdownShadowSource
                        verticalOffset: 8
                        radius: 28
                        samples: 57
                        color: "#29000000"
                        transparentBorder: true
                    }
                }

                WidgetTheme.Widget {
                    id: countdownPreview
                    anchors.horizontalCenter: parent.horizontalCenter
                    // Layout.preferredWidth: 150
                    text: qsTr("Remaining")
                    property real initialProgress: 0.2 + Math.random() * 0.5
                    property int remainingSeconds: 180 + Math.floor(Math.random() * 721)
                    property int totalSeconds: Math.ceil(remainingSeconds / initialProgress)

                    Timer {
                        interval: 1000
                        running: countdownPreview.remainingSeconds > 0
                        repeat: true
                        onTriggered: countdownPreview.remainingSeconds--
                    }

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 2

                        RowLayout {
                            Layout.alignment: Qt.AlignHCenter
                            spacing: 0

                            WidgetTheme.AnimatedDigits {
                                value: String(Math.floor(countdownPreview.remainingSeconds / 60)).padStart(2, "0")
                            }
                            WidgetTheme.Title {
                                Layout.bottomMargin: font.pixelSize * 0.1
                                text: ":"
                            }
                            WidgetTheme.AnimatedDigits {
                                value: String(countdownPreview.remainingSeconds % 60).padStart(2, "0")
                            }
                        }

                        ProgressBar {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.preferredWidth: 82
                            Layout.preferredHeight: 4
                            value: countdownPreview.remainingSeconds / countdownPreview.totalSeconds
                            primaryColor: "#e08a4f"
                        }
                    }
                }
            }

            // Fixed-data version of widgets/currentActivity.qml.
            Item {
                Layout.alignment: Qt.AlignCenter
                implicitWidth: currentActivityPreview.width
                implicitHeight: currentActivityPreview.height + 36

                Item {
                    width: currentActivityPreview.width
                    height: 36
                    anchors.top: currentActivityPreview.bottom
                    anchors.horizontalCenter: parent.horizontalCenter
                    clip: true

                    Rectangle {
                        id: currentActivityShadowSource
                        width: currentActivityPreview.width
                        height: currentActivityPreview.height
                        y: -currentActivityPreview.height
                        radius: Math.min(width, height, currentActivityPreview.cornerRadius)
                        color: "#29000000"
                        visible: false
                    }

                    DropShadow {
                        x: currentActivityShadowSource.x
                        y: currentActivityShadowSource.y
                        width: currentActivityShadowSource.width
                        height: currentActivityShadowSource.height
                        source: currentActivityShadowSource
                        verticalOffset: 8
                        radius: 28
                        samples: 57
                        color: "#29000000"
                        transparentBorder: true
                    }
                }

                WidgetTheme.Widget {
                    id: currentActivityPreview
                    anchors.horizontalCenter: parent.horizontalCenter
                    // Layout.preferredWidth: 250
                    text: qsTr("Current Activity")

                    backgroundArea: Rectangle {
                        id: activityGlow
                        width: currentActivityPreview.height * 0.4
                        height: currentActivityPreview.height * 0.4
                        x: (parent.width - width) / 2
                        y: (parent.height - height) / 2 + 8
                        radius: height / 2
                        color: "#605ed2"
                        visible: currentActivityPreview.lightingEffect
                        opacity: 0.35
                        layer.enabled: true
                        layer.effect: FastBlur {
                            anchors.fill: activityGlow
                            radius: 64
                            opacity: 0.5
                            transparentBorder: true
                        }
                    }

                    RowLayout {
                        anchors.centerIn: parent
                        spacing: 10

                        WidgetTheme.Icon {
                            name: "ic_fluent_code_20_regular"
                            size: 32
                        }
                        WidgetTheme.Title {
                            text: qsTr("Mathematics")
                        }
                    }
                }
            }

            // RowLayout {
            //     spacing: 12

            // Fixed-data version of widgets/Time.qml.
            Item {
                Layout.alignment: Qt.AlignCenter
                implicitWidth: timePreview.width
                implicitHeight: timePreview.height + 36

                Item {
                    width: timePreview.width
                    height: 36
                    anchors.top: timePreview.bottom
                    anchors.horizontalCenter: parent.horizontalCenter
                    clip: true

                    Rectangle {
                        id: timeShadowSource
                        width: timePreview.width
                        height: timePreview.height
                        y: -timePreview.height
                        radius: Math.min(width, height, timePreview.cornerRadius)
                        color: "#29000000"
                        visible: false
                    }

                    DropShadow {
                        x: timeShadowSource.x
                        y: timeShadowSource.y
                        width: timeShadowSource.width
                        height: timeShadowSource.height
                        source: timeShadowSource
                        verticalOffset: 8
                        radius: 28
                        samples: 57
                        color: "#29000000"
                        transparentBorder: true
                    }
                }

                WidgetTheme.Widget {
                    id: timePreview
                    anchors.horizontalCenter: parent.horizontalCenter
                    // Layout.preferredWidth: 190
                    property date currentDateTime: new Date()
                    text: Qt.locale().toString(currentDateTime, "MMMM d")

                    Timer {
                        interval: 500
                        running: true
                        repeat: true
                        onTriggered: timePreview.currentDateTime = new Date()
                    }

                    RowLayout {
                        anchors.centerIn: parent
                        spacing: 0

                        WidgetTheme.AnimatedDigits {
                            value: String(timePreview.currentDateTime.getHours()).padStart(2, "0")
                        }
                        WidgetTheme.Title {
                            Layout.bottomMargin: font.pixelSize * 0.1
                            text: ":"
                        }
                        WidgetTheme.AnimatedDigits {
                            value: String(timePreview.currentDateTime.getMinutes()).padStart(2, "0")
                        }
                        WidgetTheme.Title {
                            Layout.bottomMargin: font.pixelSize * 0.1
                            text: ":"
                        }
                        WidgetTheme.AnimatedDigits {
                            value: String(timePreview.currentDateTime.getSeconds()).padStart(2, "0")
                        }
                    }
                }
            }


            // }
        }
    }
}
