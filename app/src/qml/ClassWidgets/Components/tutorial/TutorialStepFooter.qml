import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RinUI

RowLayout {
    id: root

    property int currentStep: 1
    property int totalSteps: 5
    property bool backVisible: true
    property bool nextEnabled: true
    property string nextText: qsTr("Continue")
    property string nextIcon: "ic_fluent_arrow_right_20_regular"
    property bool secondaryVisible: false
    property bool secondaryEnabled: true
    property string secondaryText: ""
    property string secondaryIcon: ""

    signal backRequested()
    signal secondaryRequested()
    signal nextRequested()

    Layout.fillWidth: true
    // spacing: 12

    Item { Layout.fillWidth: true }

    Button {
        visible: root.backVisible
        Layout.preferredHeight: 36
        // flat: true
        icon.name: "ic_fluent_arrow_left_20_regular"
        // text: qsTr("Back")
        onClicked: root.backRequested()
    }

    Button {
        visible: root.secondaryVisible
        enabled: root.secondaryEnabled
        icon.name: root.secondaryIcon
        text: root.secondaryText
        onClicked: root.secondaryRequested()
    }

    Button {
        highlighted: true
        enabled: root.nextEnabled
        icon.name: root.nextIcon
        text: root.nextText
        onClicked: root.nextRequested()
    }

    Item { Layout.fillWidth: true }

}
