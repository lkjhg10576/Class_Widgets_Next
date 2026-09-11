import QtQuick
import QtQuick.Layouts
import RinUI

Item {
    id: root

    property string iconName: ""
    property string title: ""
    property bool showArrow: true
    property alias content: contentSlot.data

    signal activated()
    clip: false

    implicitHeight: 64
    Layout.fillWidth: true
    Layout.preferredHeight: 64

    Rectangle {
        anchors.fill: parent
        color: Colors.proxy.controlAltTertiaryColor
        opacity: tapHandler.pressed ? 0.15 : hoverHandler.hovered ? 0.75 : 0
    }

    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        color: Colors.proxy.controlBorderColor
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 22
        anchors.rightMargin: 16
        spacing: 14

        Icon {
            Layout.preferredWidth: 22
            Layout.preferredHeight: 22
            visible: root.iconName.length > 0
            name: root.iconName
            size: 22
        }

        Text {
            Layout.fillWidth: true
            typography: Typography.Body
            text: root.title
            elide: Text.ElideRight
        }

        RowLayout {
            id: contentSlot
            spacing: 16
        }

        Icon {
            Layout.preferredWidth: 18
            Layout.preferredHeight: 18
            visible: root.showArrow
            name: "ic_fluent_chevron_right_20_regular"
            size: 18
            opacity: 0.85
        }
    }

    HoverHandler {
        id: hoverHandler
    }

    TapHandler {
        id: tapHandler
        onTapped: root.activated()
    }
}
