import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RinUI
import ClassWidgets.Theme 1.0


Text {
    id: text

    property var baseFont: AppCentral.getQFont(
        Configs.data.preferences.font,
        Utils.fontFamily
    )

    font.family: baseFont.family
    font.weight: Configs.data.preferences.font_weight || 600
    font.pixelSize: px
}
