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

    readonly property string eventTitle: (settings && settings.title) ? settings.title : ""
    readonly property string targetDate: (settings && settings.target_date) ? settings.target_date : ""
    readonly property bool configured: targetDate.length > 0

    // 仅用于让 daysLeft 绑定挂到秒心跳上（跨零点自动重算；天数本身只在日切时变化）
    property date today: new Date()

    // 目标日 0 点 − 今日 0 点，按本地日历差取整（不随当天时刻漂移）
    readonly property int daysLeft: {
        today
        if (!configured)
            return 0
        const parts = targetDate.split("-")
        if (parts.length !== 3)
            return 0
        const y = parseInt(parts[0]), m = parseInt(parts[1]), d = parseInt(parts[2])
        if (isNaN(y) || isNaN(m) || isNaN(d))
            return 0
        const target = new Date(y, m - 1, d)
        const now = new Date()
        const midnight = new Date(now.getFullYear(), now.getMonth(), now.getDate())
        return Math.round((target.getTime() - midnight.getTime()) / 86400000)
    }

    // 上方标题行："距离（标题）还有"；未设置事件名时退化为组件名。
    // 引用 translator.language 使语言切换即时重算（对齐 currentActivity.qml 先例）
    text: {
        AppCentral.translator.language
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
            visible: root.configured
            text: {
                AppCentral.translator.language
                return qsTr("%1 days").arg(root.daysLeft)
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
