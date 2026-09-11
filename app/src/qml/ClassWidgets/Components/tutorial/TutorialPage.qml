import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RinUI

Item {
    id: root
    transformOrigin: Item.Center

    property var tutorial
    property string title: ""
    property string description: ""
    property int currentStep: 1
    property int totalSteps: 1
    property bool allowBack: true
    property bool allowNext: true
    property bool managesNextNavigation: false
    property string nextText: qsTr("Continue")
    property string nextIcon: "ic_fluent_arrow_right_20_regular"
    property bool footerSecondaryVisible: false
    property bool footerSecondaryEnabled: true
    property string footerSecondaryText: ""
    property string footerSecondaryIcon: ""
    property real leftColumnRatio: 0.55
    property Component rightContent: null
    property bool fillOperationHeight: false
    property alias icon: ricon
    property real pageTransitionOffset: 0
    property real visualTransitionOpacity: 1

    default property alias operationContent: operationContentArea.data

    signal backRequested()
    signal footerSecondaryRequested()
    signal nextRequested()

    function requestBack() {
        backRequested()
    }

    function requestNext() {
        nextRequested()
    }

    RowLayout {
        anchors.fill: parent

        spacing: 0

        ColumnLayout {
            id: leftArea
            // Layout.fillWidth: true
            Layout.preferredWidth: parent.width * leftColumnRatio
            Layout.fillHeight: true
            Layout.margins: 46
            Layout.leftMargin: 56
            Layout.rightMargin: 56
            spacing: 16

            Item {
                id: leftContentViewport
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                ColumnLayout {
                    id: leftContentArea
                    width: Math.min(parent.width, 600)
                    height: parent.height
                    anchors.top: parent.top
                    x: (parent.width - width) / 2 + root.pageTransitionOffset
                    spacing: 16

                    ColumnLayout {
                        spacing: 4
                        Text {
                            text: root.title
                            typography: Typography.Title
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: root.description.length > 0
                            text: root.description
                            typography: Typography.Body
                            // color: Colors.proxy.textSecondaryColor
                            wrapMode: Text.WordWrap
                        }
                    }

                    Flickable {
                        id: leftOperationArea
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        contentWidth: width
                        contentHeight: Math.max(height, operationContentArea.implicitHeight)
                        flickableDirection: Flickable.VerticalFlick
                        boundsBehavior: Flickable.StopAtBounds

                        ColumnLayout {
                            id: operationContentArea
                            width: leftOperationArea.width
                            height: root.fillOperationHeight
                                    ? Math.max(leftOperationArea.height, implicitHeight)
                                    : implicitHeight
                            spacing: 0
                        }

                        ScrollBar.vertical: ScrollBar {
                            policy: ScrollBar.AsNeeded
                        }
                    }
                }
            }

            TutorialStepFooter {
                currentStep: root.currentStep
                totalSteps: root.totalSteps
                backVisible: root.allowBack
                nextEnabled: root.allowNext
                nextText: root.nextText
                nextIcon: root.nextIcon
                secondaryVisible: root.footerSecondaryVisible
                secondaryEnabled: root.footerSecondaryEnabled
                secondaryText: root.footerSecondaryText
                secondaryIcon: root.footerSecondaryIcon
                onBackRequested: root.requestBack()
                onSecondaryRequested: root.footerSecondaryRequested()
                onNextRequested: root.requestNext()
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredWidth: parent.width * (1 - root.leftColumnRatio)
            Layout.fillHeight: true

            Loader {
                id: rightContentLoader
                anchors.fill: parent
                sourceComponent: root.rightContent || defaultRightContent

                onLoaded: {
                    if (item && item.contentOpacity !== undefined)
                        item.contentOpacity = Qt.binding(function() { return root.visualTransitionOpacity })
                }
            }
        }
    }

    Icon{
        id: ricon
        visible: false
        source: PathManager.images("icons/cwn_whatsnew.png")
    }

    Component {
        id: defaultRightContent

        TutorialVisual {
            anchors.fill: parent
            contentOpacity: root.visualTransitionOpacity
            icon.name: ricon.name
            icon.source: ricon.source
        }
    }
}
