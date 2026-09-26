import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window          // Screen：宽度上限按屏幕可用宽度折算
import RinUI
import ClassWidgets.Theme
import Qt5Compat.GraphicalEffects

Widget {
    id: root
    text: {
        AppCentral.translator.language
        return qsTr("Upcoming")
    }
    property bool isExternalClass: AppCentral.scheduleRuntime.currentSubject.isLocalClassroom === false
    property bool showLeaveHint: false

    property var entries: AppCentral.scheduleRuntime.nextEntries || []
    property var subjects: AppCentral.scheduleRuntime.subjects || []

    // ---- 显示规则 ----
    // 「显示缩写」开关：沿用上游存量键 full_name（键名与含义相反——false 即显示缩写，
    // 缺省按显示缩写处理），编辑界面以“显示缩写”呈现并取反绑定。
    readonly property bool showAbbreviation: (!settings || settings.full_name === undefined)
        ? true
        : !settings.full_name

    // 最多显示节数（与「最多活动数」数字框 1–7 对应；缺省/非法回退 7）
    readonly property int maxActivities: Math.max(
        1, Math.min(7, (settings && settings.max_activities) || 7))

    readonly property real entrySpacing: 16

    // 宽度上限：BaseWidget 的 implicitWidth = 内容宽 + 48（左右内边距各 24），桌面左右
    // 再各留 48 安全边距；WidgetsContainer 按 scale_factor 对组件做视觉缩放，故按其折算
    readonly property real maxContentWidth: Math.max(
        80, (Screen.width - 96) / (Configs.data.preferences.scale_factor || 1.0) - 48)

    // 每条要显示的文本；语言切换时读 translator.language 强制重算（qsTr 不会自动触发绑定）
    readonly property var displayTexts: {
        AppCentral.translator.language
        if (entries.length === 0)
            return [qsTr("Nothing ahead")]
        const result = []
        const count = Math.min(entries.length, maxActivities)
        for (let i = 0; i < count; i++)
            result.push(resolveEntryText(entries[i]))
        return result
    }

    // refit() 结果：能完整放下的条数 / 首条是否超出宽度上限 / 首条省略后的文本
    // 注意：不能直接给 delegate 设 elide——elide 会让 Text 的 implicitWidth 反向依赖
    // width，与 width: implicitWidth 形成绑定环（运行时实测），故省略号在 refit 里
    // 用 TextMetrics 预生成，delegate 本身不带 elide。
    property int fittingCount: 0
    property bool elideFirst: false
    property string firstElided: ""

    function subjectById(id) {
        for (let i = 0; i < subjects.length; i++) {
            if (subjects[i].id === id)
                return subjects[i]
        }
        return null
    }

    function fallbackText(type) {
        return type === "class" ? qsTr("Class")
             : type === "activity" ? qsTr("Activity")
             : qsTr("Unset")
    }

    function resolveEntryText(entry) {
        const subject = subjectById(entry.subjectId)
        const fromTitle = !!entry.title
        const full = entry.title || (subject && subject.name) || fallbackText(entry.type)
        if (!showAbbreviation)
            return full
        // 文本来自科目全称且该科目设了缩写 → 原样用（多字缩写不截断，如「现代」）
        if (!fromTitle && subject && subject.simplifiedName)
            return subject.simplifiedName
        // 其余情况取第一个字；用 Array.from 而非 charAt(0)，避免劈开代理对
        const chars = Array.from(full)
        return chars.length ? chars[0] : full
    }

    // 逐条累加宽度，放不下就停 —— 已显示的课程名保证完整（不截断文字）
    // 保底：首条就超宽时用 TextMetrics 生成省略文本（Qt 6.10：advanceWidth / elide+elideWidth+elidedText）
    function refit() {
        const total = displayTexts.length
        if (total === 0) {
            fittingCount = 0
            elideFirst = false
            firstElided = ""
            return
        }
        measure.elide = Qt.ElideNone
        measure.elideWidth = 0
        const limit = maxContentWidth
        let used = 0
        let count = 0
        for (let i = 0; i < total; i++) {
            measure.text = displayTexts[i]
            const w = measure.advanceWidth
            const need = (count === 0 ? 0 : entrySpacing) + w
            if (count > 0 && used + need > limit)
                break
            used += need
            count++
        }
        measure.text = displayTexts[0]
        const needElide = measure.advanceWidth > limit
        let elided = ""
        if (needElide) {
            measure.elide = Qt.ElideRight
            measure.elideWidth = limit
            elided = measure.elidedText
        }
        fittingCount = count
        elideFirst = needElide
        firstElided = elided
    }

    onDisplayTextsChanged: refitTimer.restart()
    onMaxContentWidthChanged: refitTimer.restart()
    Component.onCompleted: refitTimer.restart()

    Timer {
        id: refitTimer
        interval: 0            // 同一轮事件循环内合并（课表刷新 / mini 字号动画逐帧）
        onTriggered: root.refit()
    }

    Row {
        id: entryRow
        anchors.centerIn: parent
        spacing: root.entrySpacing
        width: implicitWidth   // 交给 BaseWidget 的 childrenRect 驱动组件宽度自适应

        Repeater {
            id: entryRepeater
            model: root.displayTexts.length

            delegate: Title {
                id: del
                required property int index
                text: index === 0 && root.elideFirst ? root.firstElided
                    : (root.displayTexts[index] || "")
                visible: index < root.fittingCount
                // 宽度用 TextMetrics.advanceWidth 驱动：Text.implicitWidth 的求值会重入
                // width（运行时实测 Binding loop），不能直接 width: implicitWidth。
                width: index >= root.fittingCount ? 0 : widthMetric.advanceWidth

                TextMetrics {
                    id: widthMetric
                    text: del.text
                    font: del.font
                }

                onTextChanged: refitTimer.restart()
            }
        }
    }

    // 量宽工具（QObject，不参与 childrenRect）。字体跟随第一条文本，
    // 绑定里读的是子属性，字体变化会重新求值；字号动画期间逐帧触发 refit。
    TextMetrics {
        id: measure
        readonly property var sample: entryRepeater.count > 0 ? entryRepeater.itemAt(0) : null
        font.family: sample ? sample.font.family : ""
        font.pixelSize: sample ? sample.font.pixelSize : 28
        font.weight: sample ? sample.font.weight : Font.Normal
        font.italic: sample ? sample.font.italic : false
        font.letterSpacing: sample ? sample.font.letterSpacing : 0
        onFontChanged: refitTimer.restart()
    }
}
