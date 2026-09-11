import QtQuick
import QtQuick.Effects
import QtQuick.Shapes
import RinUI

Item {
    id: root

    Rectangle {
        id: background
        anchors.fill: parent
        // 动态判断主题切换渐变
        // opacity:  Theme.isDark() ? 0.2 : 0.8
        gradient: Theme.isDark() ? darkGradient : lightGradient

        // 1. 定义亮色主题渐变
        Gradient {
            id: lightGradient
            GradientStop { position: 0.0; color: "#72BBCF" }
            GradientStop { position: 0.8; color: Qt.alpha("#72BBCF", 0) }
        }

        // 2. 定义暗色主题渐变
        Gradient {
            id: darkGradient
            GradientStop { position: 0.0; color: Qt.alpha("#0A343F", 1) }
            GradientStop { position: 0.8; color: Qt.alpha("#0A343F", 0) }
        }
    }


    Rectangle {
        // sun light
        anchors.fill: parent
        visible: !Theme.isDark()
        gradient: RadialGradient {
            centerX: root.width * 0.34
            centerY: 0
            focalX: centerX
            focalY: centerY
            focalRadius: Math.max(root.width, root.height) * 0.92

            GradientStop { position: 0.0; color: "#00fbfbf7" }
            GradientStop { position: 1.0; color: "#fbfbf7" }
        }
    }

}
