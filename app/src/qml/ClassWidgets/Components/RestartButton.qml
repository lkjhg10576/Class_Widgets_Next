import QtQuick
import QtQuick.Controls
import RinUI


Button {
    id: root

    // 仅在有待应用的重启时显示
    visible: AppCentral.restartRequired
    highlighted: true
    flat: true
    // backgroundColor: Colors.proxy.systemCautionBackgroundColor
    icon.name: "ic_fluent_arrow_counterclockwise_20_regular"
    text: qsTr("Restart required")

    ToolTip {
        text: qsTr("Restart to apply changes")
        visible: parent.hovered
    }

    onClicked: AppCentral.restart()
}
