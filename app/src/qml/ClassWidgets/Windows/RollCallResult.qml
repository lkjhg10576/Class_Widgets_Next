import QtQuick
import QtQuick.Window as QQW

// 随机点名结果窗（阶段 C4，extensions-feature-plan §6）：显示最近一次抽中
// 名单，支持"再点 N 名"与会话清空。窗口与 WindowManager 的协作同
// RollCallFloat/Settings.qml（onClosing 拦截转 close 槽，销毁走 0ms 延迟释放）。
QQW.Window {
    id: resultWindow
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool
    color: "transparent"

    onClosing: function(event) {
        event.accepted = false
        resultWindow.closeAndReset()
    }

    // ─────────────────────────── 数据 ───────────────────────────
    // 结果取 RollCall.lastDraw（NOTIFY drawCompleted，绑定随每次 draw 自动刷新，
    // 悬浮窗与本页"再点"两条路径共用同一来源）
    readonly property var drawn: RollCall ? RollCall.lastDraw : []
    // 最近一次请求人数（先引用 drawn 注册依赖：每次 drawCompleted 后重读
    // lastRequested()，用于"名单人数不足"提示。N 超员时服务端抽全部，
    // drawn.length < requested 即该边界）
    readonly property int requested: {
        resultWindow.drawn
        return RollCall ? RollCall.lastRequested() : 0
    }

    // ─────────────────────────── 屏幕与居中 ───────────────────────────
    // 选屏与 MainInterface.qml:27-34 / RollCallFloat.qml 同源：
    // preferences.display 匹配 Qt.application.screens，未命中回退第一块屏。
    // 窗口取该屏虚拟坐标原点 + (屏 - 窗)/2；C++ 侧无现成定位工具（§6 C4），
    // 定位全部放 QML。首开在 Component.onCompleted 定一次；单例复用（open →
    // 已存在仅 show/raise）或换屏后由 onVisibleChanged/onTargetScreenChanged
    // 再居中（屏幕未变时结果与上次一致，重设幂等）
    property string screenName: Configs.data.preferences.display || Qt.application.screens[0].name
    property var targetScreen: {
        for (let s of Qt.application.screens) {
            if (s.name === screenName)
                return s
        }
        return Qt.application.screens[0]
    }

    width: resultWindow.targetScreen
          ? Math.min(600, resultWindow.targetScreen.width - 80) : 600
    height: 440

    function centerOnScreen() {
        const s = resultWindow.targetScreen
        if (!s)
            return
        resultWindow.x = s.virtualX + (s.width - resultWindow.width) / 2
        resultWindow.y = s.virtualY + (s.height - resultWindow.height) / 2
    }

    Component.onCompleted: centerOnScreen()
    onVisibleChanged: if (visible) centerOnScreen()
    onTargetScreenChanged: centerOnScreen()
    onWidthChanged: if (visible) centerOnScreen()

    // ─────────────────────────── 动作 ───────────────────────────

    function rollMore(count) {
        if (RollCall)
            RollCall.draw(count)
        // session 模式下排除逻辑在服务端：已点过的人不会再出现在 drawn 里；
        // 全部点完后 drawn 为空 → 卡片内显示引导文案
    }

    // 关闭：清会话排除名单（§6 C4："结果窗口关闭时清空会话"），再走管理器
    // 的销毁路径（0ms singleShot 释放，QML 回调栈内不拆对象树）
    function closeAndReset() {
        if (RollCall)
            RollCall.clearSession()
        WindowManager.closeRollCallResult()
    }

    // 结果卡片：不透明深底浮在透明窗口上，无边框窗口的"窗口感"由这张卡片给
    Rectangle {
        anchors.fill: parent
        anchors.margins: 12
        radius: 20
        color: Qt.rgba(0.09, 0.10, 0.15, 0.96)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.10)

        Column {
            anchors.centerIn: parent
            width: parent.width - 48
            spacing: 18

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("随机点名")
                color: Qt.rgba(1, 1, 1, 0.65)
                font.pixelSize: 16
                font.bold: true
            }

            // 抽中名单：大字号逐行；1~3 人纵向排开最稳（不挤、不换行截断）
            Repeater {
                model: resultWindow.drawn

                delegate: Text {
                    required property var modelData
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: modelData.name
                    color: "white"
                    font.pixelSize: 52
                    font.bold: true
                }
            }

            // 空态：名单为空，或 session 模式本会话已点完
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: resultWindow.drawn.length === 0
                text: qsTr("没有可点的人：请在设置中导入名单，或关闭窗口开始新会话")
                color: Qt.rgba(1, 1, 1, 0.75)
                font.pixelSize: 16
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
            }

            // 人数不足边界（§6 C1：N 超过可抽人数 → 抽全部，结果长度体现）
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: resultWindow.drawn.length > 0
                         && resultWindow.drawn.length < resultWindow.requested
                text: qsTr("名单人数不足，已抽出全部 %1 人").arg(resultWindow.drawn.length)
                color: Qt.rgba(1, 0.80, 0.40, 1)
                font.pixelSize: 13
            }

            Item { width: 1; height: 6 }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 10

                MiniButton { label: qsTr("再点 1 名"); onClicked: resultWindow.rollMore(1) }
                MiniButton { label: qsTr("再点 2 名"); onClicked: resultWindow.rollMore(2) }
                MiniButton { label: qsTr("再点 3 名"); onClicked: resultWindow.rollMore(3) }
                MiniButton { label: qsTr("关闭"); muted: true
                             onClicked: resultWindow.closeAndReset() }
            }
        }
    }

    // 扁平小按钮：与 RollCallFloat.qml 同款自绘（新窗口只有这一处交互面，
    // 自绘避免为轻量窗口引入整套控件样式；两处各留一份内联组件）
    component MiniButton: Rectangle {
        property string label: ""
        property bool muted: false
        signal clicked()

        width: Math.max(72, labelText.implicitWidth + 28)
        height: 42
        radius: 10
        color: muted ? (btnHover.hovered ? Qt.rgba(1, 1, 1, 0.20) : Qt.rgba(1, 1, 1, 0.10))
                     : (btnHover.hovered ? Qt.rgba(0.98, 0.98, 0.98, 1) : Qt.rgba(0.90, 0.90, 0.92, 1))
        Behavior on color { ColorAnimation { duration: 100 } }

        Text {
            id: labelText
            anchors.centerIn: parent
            text: parent.label
            color: parent.muted ? "white" : "#1b1b1f"
            font.pixelSize: 15
            font.bold: !parent.muted
        }

        HoverHandler { id: btnHover }
        TapHandler { onTapped: parent.clicked() }
    }
}
