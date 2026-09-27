import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RinUI
import ClassWidgets.Theme

Widget {
    id: root
    // 倒数日组件（本移植新增内置组件，对齐 weather.qml 的"新增文件不动上游"纪律）。
    // settings 契约（BuiltinWidgets.cpp 注册 defaultSettings）：
    //   title        事件名，字符串
    //   target_date  目标日，"yyyy-MM-dd" 字符串（未设置为空 → 显示右键提示）
    //   cycle        重复周期，"none" | "weekly" | "monthly" | "yearly"（缺省 "none"，
    //                存量配置无此键时与旧行为一致）；循环型自动滚动到下一次发生日。
    //
    // 显示状态机：
    //   未配置 → 默认名 + 右键提示；未来 → "距离xx还有 N 天"；
    //   就是今天 → 事件名 + "就是今天"；已过（仅不重复）→ "距离xx已过 N 天"（正数）。

    readonly property string eventTitle: (settings && settings.title) ? settings.title : ""
    readonly property string targetDate: (settings && settings.target_date) ? settings.target_date : ""
    readonly property string cycle: (settings && settings.cycle) ? settings.cycle : "none"
    readonly property bool configured: targetDate.length > 0

    // 仅用于让 effectiveParts 绑定挂到秒心跳上（跨零点自动重算；天数本身只在日切时变化）
    property date today: new Date()

    function daysInMonth(y, m) { // m: 1-12
        return new Date(y, m, 0).getDate()
    }

    function parseTarget() {
        const parts = targetDate.split("-")
        if (parts.length !== 3)
            return null
        const y = parseInt(parts[0]), m = parseInt(parts[1]), d = parseInt(parts[2])
        if (isNaN(y) || isNaN(m) || isNaN(d) || m < 1 || m > 12 || d < 1 || d > 31)
            return null
        return { y: y, m: m, d: d }
    }

    // 下一次发生日（分量表示）。不重复直接返回目标日分量（日号钳制到当月）；
    // 循环型向前滚动到 >= 今日 0 点。全部按"年月日分量"推进，避免毫秒加减跨
    // 夏令时切换日的 23/25 小时偏差；guard 防止异常配置死循环。
    readonly property var effectiveParts: {
        today
        const t = parseTarget()
        if (!t)
            return null
        const now = new Date()
        const midnight = new Date(now.getFullYear(), now.getMonth(), now.getDate()).getTime()
        function atMs(y, m, d) {
            return new Date(y, m - 1, Math.min(d, daysInMonth(y, m))).getTime()
        }
        if (cycle === "weekly") {
            let cur = new Date(t.y, t.m - 1, Math.min(t.d, daysInMonth(t.y, t.m)))
            let guard = 0
            while (cur.getTime() < midnight && guard++ < 5200) {
                const next = new Date(cur.getFullYear(), cur.getMonth(), cur.getDate() + 7)
                cur = next
            }
            return { y: cur.getFullYear(), m: cur.getMonth() + 1, d: cur.getDate() }
        }
        if (cycle === "monthly") {
            let y = t.y, m = t.m
            let guard = 0
            while (atMs(y, m, t.d) < midnight && guard++ < 1200) {
                m += 1
                if (m > 12) { m = 1; y += 1 }
            }
            // 小月顺延到月末最后一天（如 31 日 → 4 月按 30 日）
            return { y: y, m: m, d: Math.min(t.d, daysInMonth(y, m)) }
        }
        if (cycle === "yearly") {
            let y = t.y
            let guard = 0
            while (atMs(y, t.m, t.d) < midnight && guard++ < 300) {
                y += 1
            }
            // 平年 2 月 29 日顺延为 2 月 28 日
            return { y: y, m: t.m, d: Math.min(t.d, daysInMonth(y, t.m)) }
        }
        return { y: t.y, m: t.m, d: Math.min(t.d, daysInMonth(t.y, t.m)) }
    }

    // 下一次发生日 0 点 − 今日 0 点，按本地日历差取整（不随当天时刻漂移）；
    // 仅不重复时可能为负（已过），循环型恒 >= 0。
    readonly property int daysLeft: {
        today
        if (!effectiveParts)
            return 0
        const target = new Date(effectiveParts.y, effectiveParts.m - 1, effectiveParts.d)
        const now = new Date()
        const midnight = new Date(now.getFullYear(), now.getMonth(), now.getDate())
        return Math.round((target.getTime() - midnight.getTime()) / 86400000)
    }

    // 上方标题行。引用 translator.language 使语言切换即时重算
    //（对齐 currentActivity.qml 先例）。
    text: {
        AppCentral.translator.language
        if (!configured)
            return qsTr("Days Countdown")
        if (daysLeft < 0)
            return eventTitle.length > 0
                ? qsTr("Since %1").arg(eventTitle)
                : qsTr("Days Countdown")
        if (daysLeft === 0)
            return eventTitle.length > 0 ? eventTitle : qsTr("Days Countdown")
        return eventTitle.length > 0
            ? qsTr("Until %1").arg(eventTitle)
            : qsTr("Days Countdown")
    }

    // A6 纪律：不自开 QTimer，订阅全局秒心跳
    Connections {
        target: UnionTimer
        function onTick() {
            root.today = new Date()
        }
    }

    RowLayout {
        anchors.centerIn: parent
        spacing: 4

        Title {
            visible: root.configured && root.daysLeft !== 0
            text: {
                AppCentral.translator.language
                return qsTr("%1 days").arg(Math.abs(root.daysLeft))
            }
        }

        Title {
            visible: root.configured && root.daysLeft === 0
            text: {
                AppCentral.translator.language
                return qsTr("Today is the day")
            }
        }

        Subtitle {
            visible: !root.configured
            text: {
                AppCentral.translator.language
                return qsTr("Right-click to set a date")
            }
        }
    }
}
