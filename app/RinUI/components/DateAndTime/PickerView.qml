import QtQuick 2.15
import QtQuick.Controls.Basic 2.15
import QtQuick.Layouts 2.15
import "../../themes"
import "../../components"

Popup {
    id: root

    width: 300
    height: 330
    implicitHeight: 330

    // 覆盖式对齐：高亮条中心与触发按钮中心重合；放不下时由基类回退到 Bottom/Top
    position: Position.AlignCenter

    // 高亮条（Tumbler 高亮区）中心相对 popup 几何中心的补偿：
    // popup 中心比高亮条中心低 (buttonRow.height + 分割线) / 2
    alignOffsetY: (buttonRow.implicitHeight + 1) / 2

    property var value1: undefined
    property var value2: undefined
    property var value3: undefined

    // 当前三列（视觉左→右）对应的 Tumbler。由 Repeater onCompleted 填充。
    readonly property var _tumblers: [null, null, null]

    function _tumblerForSlot(slot) {
        for (let i = 0; i < root._tumblers.length; ++i) {
            let t = root._tumblers[i]
            if (t && t.slot === slot) return t
        }
        return null
    }

    property int index1: {
        let t = _tumblerForSlot(1)
        return t ? t.currentIndex : _slotIndexOfValue(1, value1)
    }
    property int index2: {
        let t = _tumblerForSlot(2)
        return t ? t.currentIndex : _slotIndexOfValue(2, value2)
    }
    property int index3: {
        let t = _tumblerForSlot(3)
        if (t) return t.currentIndex
        // slot 3 被隐藏（yearVisible=false）时依然要有合理的 fallback，供 model2 计算天数等
        return typeof model3 !== "undefined"
            ? _slotIndexOfValue(3, value3)
            : -1
    }

    property var model1: 12
    property var model2: 60
    property var model3: [qsTr("AM"), qsTr("PM")]

    // 列顺序：左→右依次展示的 slot。slot=1 对应 value1/model1，slot=2/3 同理。
    // model3 为 undefined 时 slot 3 会自动隐藏。
    property var columnOrder: [1, 2, 3]

    property bool gotData: typeof value1!== "undefined" && typeof value2!== "undefined"

    signal valueChanged(var value1, var value2, var value3)

    function formatText(count, modelData) {
        let data = modelData;
        return data.toString().length < 2 && count === 60  ? "0" + data
            : data === 0 && count === 12 ? 12 : data
    }

    // 根据 slot 号获取对应 model/value/currentIndex 的读写
    function _slotModel(slot) {
        if (slot === 1) return model1
        if (slot === 2) return model2
        if (slot === 3) return model3
        return undefined
    }
    function _slotValue(slot) {
        if (slot === 1) return value1
        if (slot === 2) return value2
        if (slot === 3) return value3
        return undefined
    }
    function _slotIndexOfValue(slot, value) {
        let m = _slotModel(slot)
        if (typeof m === "undefined") return 0
        if (typeof value === "undefined") return 0
        if (typeof m === "number") return value
        // 数组：对 value3 年份可能元素是数字，其他一般是字符串
        let idx = m.indexOf(value)
        if (idx >= 0) return idx
        if (typeof value === "string") {
            let n = parseInt(value)
            if (!Number.isNaN(n)) {
                idx = m.indexOf(n)
                if (idx >= 0) return idx
            }
        }
        return 0
    }

    property int visibleItemCount: 7

    // 数字/文字 选择 //
    Component {
        id: delegateComponent

        Text {
            readonly property bool highlighted: Tumbler.displacement < 0.5 && Tumbler.displacement > -0.5
            text: formatText(Tumbler.tumbler.count, modelData)
            color: highlighted? Theme.currentTheme.colors.textOnAccentColor : Theme.currentTheme.colors.textColor
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter

            // 点击选择喵 看看啥时候把背景加上
            MouseArea {
                anchors.fill: parent
                onClicked: Tumbler.tumbler.currentIndex = index
            }
        }
    }
    padding: 0

    ColumnLayout {
        id: columnLayout
        anchors.fill: parent
        spacing: -2

        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: 0
            leftPadding: 4
            rightPadding: 4

            frameless: true
            background: Rectangle {
                id: highlightBackground
                anchors.centerIn: parent
                height: 40
                radius: Theme.currentTheme.appearance.buttonRadius
                color: Theme.currentTheme.colors.primaryColor
                width: parent.width - parent.leftPadding - parent.rightPadding
            }

            RowLayout {
                id: tumblerRow
                anchors.fill: parent
                spacing: 0

                Repeater {
                    id: tumblerRepeater
                    model: {
                        // 过滤掉 slot=3 且 model3 为 undefined 的情况
                        return columnOrder.filter(function (slot) {
                            return slot !== 3 || typeof model3 !== "undefined"
                        })
                    }

                    delegate: Item {
                        readonly property int slot: modelData
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        // 列之间的分隔符（最后一列不显示；slot 3 隐藏时，原位于其后的分隔符也不显示）
                        ToolSeparator {
                            Layout.fillHeight: true
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            implicitHeight: parent.height
                            visible: index !== tumblerRepeater.count - 1
                        }

                        Tumbler {
                            id: _col
                            anchors.fill: parent
                            property int slot: parent.slot
                            model: _slotModel(slot)
                            visibleItemCount: root.visibleItemCount
                            delegate: delegateComponent
                        }

                        Component.onCompleted: {
                            // 将 Tumbler 按视觉顺序登记到 _tumblers；注意这里 index 是视觉顺序
                            if (index >= 0 && index < 3) {
                                root._tumblers[index] = _col
                            }
                        }
                        Component.onDestruction: {
                            if (index >= 0 && index < 3 && root._tumblers[index] === _col) {
                                root._tumblers[index] = null
                            }
                        }
                    }
                }
            }
        }

        Rectangle {  // 分割线
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.currentTheme.colors.dividerBorderColor
        }

        // 确认/取消 按钮区域
        RowLayout {
            id: buttonRow
            Layout.fillWidth: true
            Layout.leftMargin: 4
            Layout.rightMargin: 4
            spacing: 0

            // confirm
            ToolButton {
                Layout.fillWidth: true
                flat: true
                icon.name: "ic_fluent_checkmark_20_regular"
                onClicked: {
                    for (let i = 0; i < tumblerRepeater.count; ++i) {
                        let tmb = root._tumblers[i]
                        if (!tmb) continue
                        let txt = tmb.currentItem ? tmb.currentItem.text : undefined
                        if (tmb.slot === 1) root.value1 = txt
                        else if (tmb.slot === 2) root.value2 = txt
                        else if (tmb.slot === 3) root.value3 = txt
                    }
                    root.valueChanged(root.value1, root.value2, root.value3)
                    root.close()
                }
            }
            ToolSeparator {
                implicitHeight: 40
            }
            // cancel
            ToolButton {
                Layout.fillWidth: true
                flat: true
                icon.name: "ic_fluent_dismiss_20_regular"
                onClicked: {
                    root.close()
                }
            }
        }
    }

    enter: Transition {
        enabled: position !== Position.None
        ParallelAnimation {
            NumberAnimation {
                target: root
                property: "opacity"
                from: 0
                to: 1
                duration: Utils.appearanceSpeed
                easing.type: Easing.OutQuint
            }
            // AlignCenter（高亮条覆盖对齐）：从中间（高亮条处）向上下扩散。
            // y 与 height 同参数联动（y = posY + (H - height)/2），保证高亮条中心全程钉在按钮处：
            // height: H/2 → H, y: posY + H/4 → posY
            // Bottom：从下往上滑入(y -15)；Top：从上往下滑入(y +15)
            NumberAnimation {
                target: root
                property: "y"
                from: root._resolvedPosition === Position.AlignCenter
                    ? root.posY + root.implicitHeight / 4
                    : root.posY + (root._resolvedPosition !== Position.Center
                        ? (root._resolvedPosition === Position.Top ? 15
                            : root._resolvedPosition === Position.Bottom ? -15 : 0) : 0)
                to: root.posY
                duration: Utils.animationSpeedMiddle * 0.8
                easing.type: Easing.OutQuint
            }
            // 高度生长仅在覆盖对齐时生效（避让滑入时高度不变）
            NumberAnimation {
                target: root
                property: "height"
                from: root._resolvedPosition === Position.AlignCenter
                    ? root.implicitHeight / 2 : root.implicitHeight
                to: root.implicitHeight
                duration: Utils.animationSpeedMiddle * 0.8
                easing.type: Easing.OutQuint
            }
            ScriptAction {
                script: {
                    for (let i = 0; i < tumblerRepeater.count; ++i) {
                        let tmb = root._tumblers[i]
                        if (!tmb) continue
                        let v = _slotValue(tmb.slot)
                        let targetIdx = _slotIndexOfValue(tmb.slot, v)
                        tmb.positionViewAtIndex(targetIdx, Tumbler.Center)
                    }
                }
            }
        }
    }
}
