import QtQuick 2.15
import QtQuick.Controls.Basic 2.15
import QtQuick.Shapes as Shapes
import RinUI

ProgressBar {
    id: root
    property color primaryColor: Theme.currentTheme.colors.primaryColor
    property int radius: 2
    property int state: ProgressBar.Running
    enum State { Running, Paused, Error }
    implicitHeight: 8

    background: Rectangle {
        anchors.fill: parent
        radius: root.radius
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.alpha("#bcbcbc", 0.90) }
            GradientStop { position: 0.15; color: Qt.alpha("#d5d5d5", 0.90) }
            GradientStop { position: 0.85; color: Qt.alpha("#d5d5d5", 0.90) }
            GradientStop { position: 1.0; color: Qt.alpha("#bcbcbc", 0.90) }
        }
        border.width: 1
        border.color: "#b2b2b2"
        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: Math.max(0, root.radius - 1)
            color: Qt.alpha("#000000", 0.10)
        }
    }

    contentItem: Item {
        id: content
        height: root.height
        clip: true
        Rectangle {
            id: indicator
            height: parent.height
            radius: root.radius
            color: root.state === ProgressBar.Paused ? Theme.currentTheme.colors.systemCautionColor : root.state === ProgressBar.Error ? Theme.currentTheme.colors.systemCriticalColor : root.primaryColor
            width: root.indeterminate ? (root.state === ProgressBar.Running ? root.width / 3 : root.width) : root.visualPosition * parent.width
            x: root.indeterminate && root.state === ProgressBar.Running ? -width : 0
            Behavior on width { NumberAnimation { duration: Utils.animationSpeed; easing.type: Easing.OutCubic } }
            NumberAnimation on x { from: -indicator.width; to: root.width; duration: Utils.progressBarAnimationSpeed; loops: Animation.Infinite; easing.type: Easing.InOutQuart; running: root.indeterminate && root.state === ProgressBar.Running }
        }
        Rectangle {
            anchors.centerIn: parent
            width: Math.min(parent.width, 101)
            height: parent.height
            radius: root.radius
            opacity: Configs.data.preferences.opacity * 0.76
            gradient: Shapes.RadialGradient {
                centerX: width * 0.5; centerY: 0; focalX: centerX; focalY: centerY; focalRadius: Math.max(width, height) * 0.5
                GradientStop { position: 0.00; color: Qt.alpha("#ffffff", 0.517) }
                GradientStop { position: 0.44712; color: Qt.alpha("#ffffff", 0.405) }
                GradientStop { position: 0.47115; color: Qt.alpha("#ffffff", 0.170) }
                GradientStop { position: 0.59135; color: Qt.alpha("#ffffff", 0.0) }
                GradientStop { position: 0.826; color: Qt.alpha("#ffffff", 0.0) }
                GradientStop { position: 0.93269; color: Qt.alpha("#ffffff", 0.244) }
                GradientStop { position: 1.00; color: Qt.alpha("#ffffff", 0.473) }
            }
        }
    }
}
