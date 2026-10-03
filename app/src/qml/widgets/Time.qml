import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RinUI
import ClassWidgets.Theme

Widget {
    id: root
    // text: qsTr("time")
    // four-plugins B（P1 时间增强，去补丁化：Behavior/显隐全部经 displayTweaks 绑定，
    // 不整文件替换）。键缺失时逐项回退上游默认（全显 + 动画开 + 并排）。
    // 扩展开关门控（质检修正）：绑定内先读 Extensions.extensions（NOTIFY
    // extensionsChanged）建立通知依赖再调 isEnabled，关闭扩展后本组绑定整体
    // 重算——timeTweaks 置空对象 ⇒ 下方所有 `=== undefined ? 默认` 分支回到
    // 上游行为（SchedulePeekBar.qml:73-76 同款范式）。
    readonly property bool extOn: {
        Extensions.extensions
        return Extensions.isEnabled("classwidgets.ext.displayTweaks")
    }
    readonly property var timeTweaks: (extOn && Configs.data.extensions
                                       && Configs.data.extensions.display_tweaks)
        ? Configs.data.extensions.display_tweaks : {}
    readonly property bool animOn: timeTweaks.time_animation === undefined ? true : !!timeTweaks.time_animation
    readonly property bool showSeconds: timeTweaks.time_show_seconds === undefined ? true : !!timeTweaks.time_show_seconds
    readonly property bool showDate: timeTweaks.time_show_date === undefined ? true : !!timeTweaks.time_show_date
    readonly property bool showYear: timeTweaks.time_show_year === undefined ? true : !!timeTweaks.time_show_year
    readonly property bool showMonth: timeTweaks.time_show_month === undefined ? true : !!timeTweaks.time_show_month
    readonly property bool showDay: timeTweaks.time_show_day === undefined ? true : !!timeTweaks.time_show_day
    readonly property bool showWeekday: timeTweaks.time_show_weekday === undefined ? true : !!timeTweaks.time_show_weekday
    // 标题模式：上游基线为"日期↔星期 3s 交替"。扩展开关关闭 → 恢复上游交替
    // （补充质检修正：原 isAlternate 缺省按 side_by_side，关闭态成"静·并排"，
    // 与上游基线不符）；扩展开启 → 按配置键（默认树 side_by_side）
    readonly property bool isAlternate: !extOn || timeTweaks.time_title_mode === "alternate"
    readonly property bool alternateFade: timeTweaks.time_alternate_animation === undefined ? false : !!timeTweaks.time_alternate_animation
    property var dateTime: {
        "year": 1900,
        "month": 1,
        "day": 1,
        "weekday": 0,
        "hour": 0,
        "minute": 0,
        "second": 0
    }

    property int titleMode: 0

    text: {
        if (!root.showDate)
            return ""
        let jsDate = new Date(dateTime.year, dateTime.month - 1, dateTime.day)

        // 分量开关：年/月/日关闭时从日期串剔除对应段（locale 串按分隔符切分兜底）
        function dateStr() {
            if (root.showYear && root.showMonth && root.showDay)
                return enabled ? Qt.locale().toString(jsDate, "MMMM d") : dateTime.month + "/" + dateTime.day
            let parts = []
            if (root.showMonth) parts.push(dateTime.month)
            if (root.showDay) parts.push(dateTime.day)
            if (parts.length === 0 && root.showYear) return String(dateTime.year)
            if (parts.length === 0) return ""
            let base = parts.join("/")
            return root.showYear ? dateTime.year + "/" + base : base
        }
        function weekdayStr() {
            if (!root.showWeekday) return ""
            return Qt.locale().dayName(dateTime.weekday, Locale.LongFormat)
        }
        if (!root.isAlternate) {
            // 并排：日期 + 星期同显（任一为空即只显另一部分）
            const d = dateStr(), w = weekdayStr()
            if (d && w) return d + " " + w
            return d || w
        }
        if (titleMode === 0) {
            return dateStr()
        } else {
            const w = weekdayStr()
            return w || dateStr()
        }
    }

    // 日期/星期显隐切换：淡入淡出仅在 alternate + 交替动画开时生效。
    // 注：Widget 基类自带 `Behavior on opacity`（Theme/components/Widget.qml），
    // 此处不得再声明同属性 Behavior（重复声明会装载报错）；OpacityAnimator 与
    // Behavior 作用于不同驱动源，可共存。淡入作用于整挂件 220ms（标题无独立句柄）。
    OpacityAnimator on opacity {
        id: titleFade
        running: false
        from: 0; to: 1
        duration: 220
    }

    Timer {
        id: titleTimer
        interval: root.timeTweaks.time_alternate_interval || 3000   // 交替间隔可配（默认 3 秒）
        running: root.isAlternate && root.showDate
        repeat: true
        onTriggered: {
            root.titleMode = (root.titleMode + 1) % 2
            if (root.alternateFade) titleFade.restart()
        }
    }

    RowLayout {
        anchors.centerIn: parent
        spacing: 0
        AnimatedDigits {
            id: hour
            value: dateTime.hour || "00"
            animEnabled: root.animOn
        }
        Title {
            Layout.bottomMargin: font.pixelSize * 0.1
            text: ":"
        }
        AnimatedDigits {
            id: minute
            value: dateTime.minute || "00"
            animEnabled: root.animOn
        }
        Title {
            visible: root.showSeconds
            Layout.bottomMargin: font.pixelSize * 0.1
            text: ":"
        }
        AnimatedDigits {
            id: second
            visible: root.showSeconds
            value: dateTime.second || "00"
            animEnabled: root.animOn
        }

        // A6（内存/唤醒优化）：500ms 无条件 Timer → 订阅 C++ UnionTimer 秒信号
        // （与整秒对齐，消除 0.5s 偏移的半频唤醒）
        Connections {
            target: UnionTimer
            function onTick() {
                dateTime = backend.getDateTime()
            }
        }
    }

    Component.onCompleted: {
        Qt.callLater(function() {
            dateTime = backend.getDateTime()
        })
    }
}