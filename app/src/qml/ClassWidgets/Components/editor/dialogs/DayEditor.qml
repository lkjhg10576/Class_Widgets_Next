import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RinUI
import ClassWidgets.Components

Dialog {
    id: dayEditor
    modal: true
    width: 500  
    title: currentId ? qsTr("Edit Timeline") : qsTr("New Timeline")

    property string currentId: ""         // 如果有 id = 编辑，否则 = 新建
    property var currentData: ({})        // 临时缓存的数据副本

    // 周循环文案格式（与 WeekSelector.qml 同步）
    property int maxWeekCycle: AppCentral.scheduleEditor.meta.maxWeekCycle
    property int roundWeek: 1
    property int customWeek: 1
    onRoundWeekChanged: checkValid()
    onCustomWeekChanged: checkValid()
    property var roundWeekOptions: []
    property string weekCycleFormat: qsTr("Week {value} of every %1 weeks").arg(maxWeekCycle)
    property string weekCyclePrefix: weekCycleFormat.split("{value}")[0]
    property string weekCycleSuffix: weekCycleFormat.split("{value}")[1]
    property string weekFormat: qsTr("Week {value}")
    property string weekPrefix: weekFormat.split("{value}")[0]
    property string weekSuffix: weekFormat.split("{value}")[1]

    function updateRoundWeekOptions() {
        var options = []
        var cycleLength = Math.max(1, maxWeekCycle)
        for (var i = 1; i <= cycleLength; i++) {
            options.push({
                text: cycleLength === 2
                    ? i === 1 ? qsTr("1") : qsTr("2")
                    : qsTr("%1").arg(i),
                value: i
            })
        }
        roundWeekOptions = options
    }
    function normalizeRoundWeek() {
        var cycleLength = Math.max(1, maxWeekCycle)
        if (roundWeek < 1) {
            roundWeek = 1
        } else if (roundWeek > cycleLength) {
            roundWeek = cycleLength
        }
    }
    onMaxWeekCycleChanged: {
        normalizeRoundWeek()
        updateRoundWeekOptions()
    }
    Component.onCompleted: {
        normalizeRoundWeek()
        updateRoundWeekOptions()
    }

    // 打开方式
    function openFor(data) {
        if (data) {
            currentId = data.id
            reload(data)
        } else {
            currentId = ""
            reload({})   // 新建时重置
        }
        open()
    }

    // 重载数据
    function reload(data) {
        currentData = data || {}
        daySegmented.currentIndex = currentData.date ? 1 : 0
        dayId.text = currentData.id || qsTr("(auto)")

        // 日期
        if (currentData.date) dayDate.selectedDate = currentData.date

        // 星期
        for (var i = 0; i < dayButtons.count; i++) {
            dayButtons.itemAt(i).checked = false
        }
        if (currentData.dayOfWeek) {
            var indices = []
            if (currentData.dayOfWeek.length !== undefined) {
                for (var i = 0; i < currentData.dayOfWeek.length; i++) {
                    var n = Number(currentData.dayOfWeek[i])
                    if (!isNaN(n)) indices.push(n - 1)
                }
            } else {
                var n = Number(currentData.dayOfWeek)
                if (!isNaN(n)) indices.push(n - 1)
            }
            for (var j = 0; j < indices.length; j++) {
                if (indices[j] >= 0 && indices[j] < dayButtons.count) {
                    dayButtons.itemAt(indices[j]).checked = true
                }
            }
        }

        // 周循环
        weekCycleTypeAll.checked = currentData.weeks === "all" || currentData.weeks === undefined || currentData.weeks === null
        weekCycleTypeRound.checked = typeof currentData.weeks === "number"
        if (weekCycleTypeRound.checked) roundWeek = Number(currentData.weeks)
        weekCycleTypeCustom.checked = Array.isArray(currentData.weeks)
        if (weekCycleTypeCustom.checked && currentData.weeks.length > 0) customWeek = Number(currentData.weeks[0])

        checkValid()
    }

    // 检查是否可以启用 Ok
    function checkValid() {
        var valid = false

        if (daySegmented.currentIndex === 0) {
            // 星期模式
            var hasDaySelected = false
            for (var i = 0; i < dayButtons.count; i++) {
                if (dayButtons.itemAt(i).checked) { hasDaySelected = true; break }
            }
            if (!hasDaySelected) valid = false
            else if (weekCycleTypeAll.checked || weekCycleTypeCustom.checked) valid = true
            else if (weekCycleTypeRound.checked && roundWeek >= 1) valid = true
        } else {
            // 日期模式
            valid = !!dayDate.selectedDate
        }

        footer.okButton.enabled = valid
    }

    ColumnLayout {
        spacing: 24
        Layout.fillWidth: true

        Segmented {
            id: daySegmented
            Layout.fillWidth: true
            onCurrentIndexChanged: checkValid()
            SegmentedItem { text: qsTr("By Week"); icon.name: "ic_fluent_calendar_week_numbers_20_regular" }
            SegmentedItem { text: qsTr("By Date"); icon.name: "ic_fluent_calendar_20_regular" }
        }

        RowLayout {
            Text { text: qsTr("ID"); width: 100 }
            TextField { id: dayId; Layout.fillWidth: true; readOnly: true;}
            visible: false
        }

        RowLayout {
            visible: daySegmented.currentIndex === 1
            Text { text: qsTr("Date"); width: 100 }

            Item { Layout.fillWidth: true }

            CalendarDatePicker {
                id: dayDate
                onSelectedDateChanged: checkValid()
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 12
            visible: daySegmented.currentIndex === 0

            ColumnLayout {
                spacing: 6
                Text { text: qsTr("Days of Week")}
                Flow {
                    Layout.fillWidth: true
                    spacing: 4
                    Repeater {
                        id: dayButtons
                        model: [
                            qsTr("Mon"), qsTr("Tue"), qsTr("Wed"),
                            qsTr("Thu"), qsTr("Fri"), qsTr("Sat"), qsTr("Sun")
                        ]
                        delegate: PillButton {
                            text: modelData
                            onCheckedChanged: dayEditor.checkValid()
                        }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                Text { text: qsTr("Week")}

                RowLayout {
                    id: weekCycleTypeColumn
                    spacing: 12
                    RadioButton { id: weekCycleTypeAll; text: qsTr("Every Week"); onCheckedChanged: dayEditor.checkValid() }
                    RadioButton { id: weekCycleTypeRound; text: qsTr("Repeat on a Cycle"); onCheckedChanged: dayEditor.checkValid() }
                    RadioButton { id: weekCycleTypeCustom; text: qsTr("One Specific Week"); onCheckedChanged: dayEditor.checkValid() }
                }

                RowLayout {
                    visible: weekCycleTypeRound.checked
                    spacing: 2
                    Text { text: weekCyclePrefix }
                    ComboBox {
                        id: weekCycleRound
                        model: dayEditor.roundWeekOptions
                        Layout.preferredWidth: 72
                        textRole: "text"
                        valueRole: "value"
                        currentIndex: Math.max(0, dayEditor.roundWeek - 1)
                        onActivated: dayEditor.roundWeek = currentIndex + 1
                    }
                    Text { text: weekCycleSuffix }
                }

                RowLayout {
                    visible: weekCycleTypeCustom.checked
                    spacing: 2
                    Text { text: weekPrefix }
                    SpinBox {
                        id: weekCycleCustom
                        from: 1
                        to: 9999
                        value: dayEditor.customWeek
                        onValueChanged: dayEditor.customWeek = value
                    }
                    Text { text: weekSuffix }
                }
            }
        }
    }

    footer: DialogButtonBox {
        standardButtons: DialogButtonBox.Ok | DialogButtonBox.Cancel
        property Button okButton: standardButton(DialogButtonBox.Ok)

        onAccepted: {
            var dayOfWeekValue = []
            var date = ""
            var weeks = undefined

            if (daySegmented.currentIndex === 0) {
                // 星期模式
                for (let i = 0; i < dayButtons.count; i++) {
                    if (dayButtons.itemAt(i).checked) dayOfWeekValue.push(i + 1)
                }
                if (weekCycleTypeAll.checked) {
                    weeks = "all"
                } else if (weekCycleTypeRound.checked) {
                    weeks = roundWeek
                } else if (weekCycleTypeCustom.checked) {
                    weeks = [customWeek]
                }
            } else {
                // 日期模式
                date = dayDate.selectedDate
                weeks = "all"
            }

            if (currentId) {
                AppCentral.scheduleEditor.updateDay(currentId, dayOfWeekValue, weeks, date)
            } else {
                AppCentral.scheduleEditor.addDay(dayOfWeekValue, weeks, date)
            }
        }
        onRejected: dayEditor.close()

        Component.onCompleted: {
            okButton.enabled = false
        }
    }
}
