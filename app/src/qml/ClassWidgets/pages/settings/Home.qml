import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RinUI
import Qt5Compat.GraphicalEffects
import ClassWidgets.Components

FluentPage {
    id: root

    horizontalPadding: 0
    wrapperWidth: Math.min(width - 42 * 2, 1000)

    function openPage(page) {
        navigationView.push(PathManager.qml("pages/settings/" + page))
    }

    contentHeader: Item {
        width: parent.width
        height: Math.max(window.height * 0.26, 200)

        Image {
            id: banner
            anchors.fill: parent
            source: PathManager.images(
                "banner/4-1_" + (Theme.isDark() ? "dark" : "light") + ".png"
            )
            fillMode: Image.PreserveAspectCrop

            layer.enabled: true
            layer.effect: OpacityMask {
                maskSource: Rectangle {
                    width: banner.width
                    height: banner.height

                    gradient: Gradient {
                        GradientStop { position: 0.7; color: "white" }
                        GradientStop { position: 1.0; color: "transparent" }
                    }
                }
            }
        }

        Text {
            anchors {
                top: parent.top
                left: parent.left
                topMargin: 38
                leftMargin: 56
            }
            typography: Typography.Title
            text: qsTr("Home")
        }
    }

    Item {
        id: waterfall
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.alignment: Qt.AlignTop

        property real spacing: 12
        property int columnCount: width < 720 ? 1 : 2
        readonly property real cardWidth: columnCount === 1 ? width : (width - spacing) / 2
        property real contentHeight: 0
        property bool relayoutPending: false

        function requestRelayout() {
            if (relayoutPending)
                return
            relayoutPending = true
            Qt.callLater(function() {
                relayoutPending = false
                relayout()
            })
        }

        function relayout() {
            var cards = [recommendedCard, personalizeCard, gettingStartedCard]
            var heights = []
            var maxBottom = 0

            for (var c = 0; c < columnCount; c++)
                heights.push(0)

            for (var i = 0; i < cards.length; i++) {
                var card = cards[i]
                var column = 0

                for (var j = 1; j < columnCount; j++) {
                    if (heights[j] < heights[column])
                        column = j
                }

                card.width = cardWidth
                card.x = column * (cardWidth + spacing)
                card.y = heights[column]

                heights[column] += card.height + spacing
                if (card.y + card.height > maxBottom)
                    maxBottom = card.y + card.height
            }

            contentHeight = maxBottom
        }

        onWidthChanged: requestRelayout()

        implicitHeight: contentHeight

        Frame {
            id: recommendedCard
            width: waterfall.cardWidth
            padding: 0
            radius: 8
            hoverable: false

            onHeightChanged: waterfall.requestRelayout()

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 32
                    Layout.margins: 22
                    spacing: 12

                    Text {
                        typography: Typography.BodyLarge
                        text: qsTr("Recommended settings")
                    }

                    Text {
                        color: Colors.proxy.textSecondaryColor
                        // typography: Typography.Caption
                        text: qsTr("Recent and commonly used settings")
                    }
                }

                Repeater {
                    model: [
                        { icon: "ic_fluent_alert_20_regular", title: qsTr("Notifications"), page: "notificationAndTime/Notification.qml" },
                        { icon: "ic_fluent_apps_add_in_20_regular", title: qsTr("Plugins"), page: "Plugins.qml" },
                        { icon: "ic_fluent_resize_20_regular", title: qsTr("Widgets"), page: "General/Widgets.qml" }
                    ]

                    delegate: ClipItem {
                        iconName: modelData.icon
                        title: modelData.title
                        onActivated: root.openPage(modelData.page)
                    }
                }
            }
        }

        Frame {
            id: personalizeCard
            width: waterfall.cardWidth
            padding: 0
            radius: 8
            hoverable: false

            onHeightChanged: waterfall.requestRelayout()

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Text {
                    Layout.topMargin: 32
                    Layout.margins: 22
                    Layout.fillWidth: true
                    Layout.preferredHeight: 30
                    typography: Typography.BodyLarge
                    text: qsTr("Personalize your widgets")
                }

                Flow {
                    Layout.fillWidth: true
                    Layout.topMargin: 0
                    Layout.margins: 22
                    spacing: 8
                    clip: true

                    Repeater {
                        model: CWThemeManager.themes

                        delegate: Rectangle {
                            id: themeCard
                            width: 112
                            height: 90
                            color: !modelData.preview ? "#ccc" : "transparent"
                            radius: Theme.currentTheme.appearance.buttonRadius
                            clip: true

                            Image {
                                anchors.fill: parent
                                source: modelData.preview || ""
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                cache: true

                                layer.enabled: true
                                layer.effect: OpacityMask {
                                    width: themeCard.width
                                    height: themeCard.height

                                    maskSource: Rectangle {
                                        width: themeCard.width
                                        height: themeCard.height
                                        radius: themeCard.radius
                                    }
                                }
                            }

                            Rectangle {
                                id: cardBorder
                                anchors.fill: parent
                                radius: themeCard.radius + 2
                                color: "transparent"
                                border.width: modelData.id === CWThemeManager.currentTheme ? 3 : 1
                                border.color: modelData.id === CWThemeManager.currentTheme
                                              ? Colors.proxy.primaryColor
                                              : Colors.proxy.controlSolidColor

                                Rectangle {
                                    anchors.fill: parent
                                    anchors.margins: 2
                                    radius: themeCard.radius
                                    color: "transparent"
                                    visible: modelData.id === CWThemeManager.currentTheme
                                    border.color: Colors.proxy.controlSolidColor
                                }
                            }

                            Rectangle {
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                anchors.margins: 6
                                width: 22
                                height: 16
                                radius: 4
                                color: modelData.color || Colors.proxy.primaryColor
                                opacity: 0.9
                            }

                            TapHandler {
                                onTapped: {
                                    if (modelData._compatible !== false) {
                                        CWThemeManager.themeChange(modelData.id)
                                        if (modelData.color)
                                            Theme.setThemeColor(modelData.color)
                                    }
                                }
                            }
                        }
                    }
                }

                ClipItem {
                    Layout.fillWidth: true
                    iconName: "ic_fluent_color_20_regular"
                    title: qsTr("Color mode")
                    showArrow: false

                    content: ComboBox {
                        enabled: !Configs.isKeyLocked("preferences.current_theme")
                        property var data: [Theme.mode.Light, Theme.mode.Dark, Theme.mode.Auto]
                        model: ListModel {
                            ListElement { text: qsTr("Light") }
                            ListElement { text: qsTr("Dark") }
                            ListElement { text: qsTr("Use system setting") }
                        }
                        currentIndex: data.indexOf(Theme.getTheme())
                        onCurrentIndexChanged: {
                            Theme.setTheme(data[currentIndex])
                        }
                    }
                }

                ClipItem {
                    Layout.fillWidth: true
                    iconName: "ic_fluent_image_20_regular"
                    title: qsTr("Browse more colors, and themes")
                    onActivated: root.openPage("Personalization.qml")
                }
            }
        }

        Frame {
            id: gettingStartedCard
            width: waterfall.cardWidth
            padding: 0
            radius: 8
            hoverable: false

            onHeightChanged: waterfall.requestRelayout()

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 32
                    Layout.margins: 22
                    spacing: 12

                    Text {
                        typography: Typography.BodyLarge
                        text: qsTr("Getting Started")
                    }

                    Text {
                        color: Colors.proxy.textSecondaryColor
                        // typography: Typography.Caption
                        text: qsTr("Complete these steps to get started")
                    }
                }

                ClipItem {
                    Layout.fillWidth: true
                    iconName: "ic_fluent_calendar_edit_20_regular"
                    title: qsTr("Set up your schedule")
                    showArrow: false
                    content: Button {
                        text: qsTr("Open schedule editor")
                        onClicked: WindowManager.openEditor()
                    }
                }

                ClipItem {
                    Layout.fillWidth: true
                    iconName: "ic_fluent_clock_20_regular"
                    title: qsTr("Calibrate time offset")
                    onActivated: root.openPage("notificationAndTime/Time.qml")
                }

                ClipItem {
                    Layout.fillWidth: true
                    iconName: "ic_fluent_alert_badge_20_regular"
                    title: qsTr("Manage notifications")
                    onActivated: root.openPage("notificationAndTime/Notification.qml")
                }
            }
        }

        Component.onCompleted: requestRelayout()
    }
}
