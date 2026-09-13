import QtQuick
import Qt5Compat.GraphicalEffects
import RinUI
import ClassWidgets.Easing


BaseWidget {
    id: root

    cornerRadius: Configs.data.preferences.widget_corner_radius
    backgroundColor: Theme.isDark()
        ? Qt.alpha("#1E1D22", 0.65)
        : Qt.alpha("#FBFAFF", 0.7)
    borderColor: Theme.isDark()
        ? Qt.alpha("#fff", 0.4)
        : Qt.alpha("#fff", 1)
    opacity: hoverHandler.hovered ? 0.8 : 1

    backgroundArea: Item {
        anchors.fill: parent

        Rectangle {
            anchors.fill: parent
            radius: root.cornerRadius
            color: root.backgroundColor
            opacity: Configs.data.preferences.opacity
        }

        // A4（内存优化）：液态玻璃描边原为 2 条"整幅渐变源 FBO + OpacityMask
        // 输出 FBO"链（每挂件常驻 4 张纹理）。环形 alpha 现直接由"透明填充 +
        // 不透明描边"的 Rectangle 自身承载 —— LinearGradient 按源 alpha 着色
        // （与渐变文字同一机制），省去 2 张遮罩输出 FBO 与 2 张整幅源 FBO，
        // 输出逐像素等价（同样的 1px 圆角环 × 同样的渐变）。
        Rectangle {
            id: outerBorderSource
            anchors.fill: parent
            radius: root.cornerRadius
            color: "transparent"
            border.width: 1
            border.color: "#fff"
            layer.enabled: true
            layer.effect: LinearGradient {
                start: Qt.point(0, 0)
                end: Qt.point(0, height)
                gradient: Gradient {
                    GradientStop { position: 0.00; color: Qt.alpha("#8d8d8d", 0.75) }
                    GradientStop { position: 1.00; color: Qt.alpha("#272727", 0.40) }
                }
            }
            opacity: Configs.data.preferences.opacity
            z: 99
        }

        Rectangle {
            id: borderRect
            anchors.fill: parent
            anchors.margins: 1
            radius: Math.max(root.cornerRadius - 1, 0)
            color: "transparent"
            border.width: root.borderWidth
            border.color: "#fff"
            layer.enabled: true
            layer.effect: LinearGradient {
                start: Qt.point(0, 0)
                end: Qt.point(width, height)
                gradient: Gradient {
                    GradientStop { position: 0; color: root.borderColor }
                    GradientStop { position: 0.5; color: Qt.alpha("#fff", 0) }
                    GradientStop { position: 0.6; color: Qt.alpha("#fff", 0) }
                    GradientStop { position: 1; color: root.borderColor }
                }
            }
            opacity: Configs.data.preferences.opacity * 1.2
            z: 99
        }
    }

    HoverHandler {
        id: hoverHandler
    }

    Behavior on implicitWidth {
        NumberAnimation {
            duration: 400
            easing.type: Easing.Bezier
            easing.bezierCurve: BezierCurve.liquidBack
        }
    }

    Behavior on height {
        NumberAnimation {
            duration: 400
            easing.type: Easing.Bezier
            easing.bezierCurve: BezierCurve.liquidBack
        }
    }

    Behavior on backgroundColor {
        ColorAnimation {
            duration: 350
            easing.type: Easing.OutQuint
        }
    }

    Behavior on borderColor {
        ColorAnimation {
            duration: 250
            easing.type: Easing.OutQuint
        }
    }

    Behavior on opacity {
        NumberAnimation {
            duration: 200
            easing.type: Easing.InOutQuad
        }
    }
}
