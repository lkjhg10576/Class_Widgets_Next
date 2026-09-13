// AnimatedDigit.qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import Qt5Compat.GraphicalEffects
import ClassWidgets.Theme 1.0

Rectangle {
    id: root
    color: "transparent"

    property string value: ""
    property string oldValue: ""
    property double progress: 1  // 0-1
    property int duration: 700
    property real scaleFactor: Configs.data.preferences.scale_factor || 1.0

    property alias font: oldDigit.font
    implicitWidth: Math.max(oldDigit.width, newDigit.width)
    implicitHeight: Math.max(oldDigit.height, newDigit.height)

    // A3（内存优化）：仅移除 layer.textureSize 的 4x 超采样（纹理缩小 16 倍，
    // Time 挂件 6 数字 12 张纹理 3.6MB → ~0.2MB）。
    // 注意：两只 Text 均 opacity 0，数字画面完全来自 LinearGradient 取样 layer
    // 纹理，且擦除动画依赖纹理常驻连续更新 —— layer.enabled 必须保持常开，
    // 按动画开关会造成每秒纹理重建、数字闪烁（2026-09-13 实测回归，勿改）。
    Title {
        id: oldDigit
        text: root.oldValue
        anchors.centerIn: parent
        opacity: 0
        layer.enabled: true
        layer.effect: null
    }

    LinearGradient  {
        id: oldDigitGradient
        anchors.fill: oldDigit
        source: oldDigit
        gradient: Gradient {
            GradientStop { position: 0; color: oldDigit.color }
            GradientStop {
                position: 0.8 - progress;
                color: Qt.alpha(oldDigit.color, Math.max(0, 1 - progress * 2))
            }
            GradientStop { position: 0.9 - progress; color: Qt.alpha(oldDigit.color, 0) }
            GradientStop { position: 1 - progress; color: Qt.alpha(oldDigit.color, 0) }
        }
    }

    Title {
        id: newDigit
        text: root.value
        anchors.centerIn: parent
        opacity: 0
        font: oldDigit.font
        layer.enabled: true
        layer.effect: null
    }

    LinearGradient  {
        id: newDigitGradient
        anchors.fill: newDigit
        opacity: progress * 3
        source: newDigit
        gradient: Gradient {
            GradientStop { position: 0.85 - progress; color: Qt.alpha(newDigit.color, 0) }
            GradientStop {
                position: 1 - progress;
                color: Qt.alpha(newDigit.color, Math.min(1, Math.max(0, progress * 2 - 0.5)))
            }
            GradientStop { position: 1; color: newDigit.color }
        }
    }

    onValueChanged: {
        newDigitGradient.visible = true
        progressAnimation.start()
    }

    SequentialAnimation {
        id: progressAnimation
        NumberAnimation {
            target: root
            property: "progress"
            from: 0
            to: 1
            duration: root.duration
            easing.type: Easing.Bezier
            easing.bezierCurve: [ .51,.2,0,.44, 1, 1 ]
        }
        ScriptAction {
            script: {
                root.oldValue = root.value
                newDigitGradient.visible = false
            }
        }
    }
}
