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
    // 保留属性（第三方主题可能引用）；A3 后不再参与 layer 纹理尺寸计算
    property real scaleFactor: Configs.data.preferences.scale_factor || 1.0
    // A3（内存优化）：layer 纹理仅在动画进行期间存在（原先常驻，且被
    // textureSize 放大 4 倍 → Time 挂件 6 数字 = 12 张 4x 超采样纹理）。
    // 注意：两只 Text 均 opacity 0，数字画面完全来自 LinearGradient 取样；
    // 静止时必须让 newDigit 直接渲染（opacity 1）并隐藏渐变，否则无任何可见内容
    property bool animating: false

    property alias font: oldDigit.font
    implicitWidth: Math.max(oldDigit.width, newDigit.width)
    implicitHeight: Math.max(oldDigit.height, newDigit.height)

    Title {
        id: oldDigit
        text: root.oldValue
        anchors.centerIn: parent
        opacity: 0
        visible: root.animating
        layer.enabled: root.animating
        layer.effect: null
    }

    LinearGradient  {
        id: oldDigitGradient
        anchors.fill: oldDigit
        visible: root.animating
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
        // 静止：直接渲染实体数字（无任何纹理）；动画期间交给渐变取样显示
        opacity: root.animating ? 0 : 1
        font: oldDigit.font
        layer.enabled: root.animating
        layer.effect: null
    }

    LinearGradient  {
        id: newDigitGradient
        anchors.fill: newDigit
        visible: root.animating
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
        root.animating = true
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
                root.animating = false
            }
        }
    }
}
