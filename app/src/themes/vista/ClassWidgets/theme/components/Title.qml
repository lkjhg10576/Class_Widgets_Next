import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RinUI
import QtQuick.Effects

Text {
    id: text
    readonly property bool miniMode: Configs.data.preferences.mini_mode
    property int px: miniMode? 20 : 28

    // font: {
    //     var f = AppCentral.getQFont(Configs.data.preferences.font, Utils.fontFamily)
    //     f.pixelSize = px
    //     f.weight = Configs.data.preferences.font_weight || 600
    //     return f
    // }
    property var baseFont: AppCentral.getQFont(
        Configs.data.preferences.font,
        Utils.fontFamily
    )

    font.family: baseFont.family
    font.weight: Configs.data.preferences.font_weight || 600
    font.pixelSize: px
    layer.enabled: true
    layer.effect: MultiEffect {
        shadowEnabled: true
        shadowHorizontalOffset: 0
        shadowVerticalOffset: 3
        shadowBlur: 0.32
        shadowColor: Qt.alpha("black", 0.25)
    }
}