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
    // four-plugins C 一期：动画时长 1-10s（ConfigStore 已钳位）+ 名单源（flicker 池）
    readonly property var rollCfg: (Configs.data.extensions && Configs.data.extensions.roll_call)
        ? Configs.data.extensions.roll_call : {}
    readonly property int animSecs: Math.min(10, Math.max(1, rollCfg.animation_seconds || 3))
    readonly property var roster: (Configs.data.extensions
                                   && Configs.data.extensions.roll_call
                                   && Configs.data.extensions.roll_call.names) || []
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

    // 坐标是否落在任一屏幕内（质检修正：原实现以 result_x/y >= 0 判定"有保存
    // 值"，副屏 virtualX 为负时有效坐标被误判为未保存，位置永不还原）
    function pointOnAnyScreen(px, py) {
        const screens = Qt.application.screens
        for (let i = 0; i < screens.length; i++) {
            const s = screens[i]
            if (px >= s.virtualX && px < s.virtualX + s.width
                    && py >= s.virtualY && py < s.virtualY + s.height)
                return true
        }
        return false
    }

    property bool geometryRestored: false

    Component.onCompleted: {
        // 位置持久化：上次结果窗几何（动态键 extensions.roll_call.result_*，未声明即动态路径）
        const rc = Configs.data.extensions ? Configs.data.extensions.roll_call : null
        const s = resultWindow.targetScreen
        if (rc && rc.result_w > 0 && rc.result_h > 0 && s) {
            // 恢复尺寸钳到屏幕可用范围（质检修正：原 Math.min(默认宽高) 只收
            // 不放，缩放手柄拖大后的尺寸永远还原不回来）
            resultWindow.width = Math.max(320, Math.min(rc.result_w, s.width - 80))
            resultWindow.height = Math.max(240, Math.min(rc.result_h, s.height - 80))
        }
        if (rc && rc.result_x !== undefined && rc.result_x !== null
                && rc.result_y !== undefined && rc.result_y !== null
                && resultWindow.pointOnAnyScreen(rc.result_x, rc.result_y)) {
            resultWindow.x = rc.result_x
            resultWindow.y = rc.result_y
            resultWindow.geometryRestored = true
        } else {
            centerOnScreen()
        }
        resultWindow.startReveal()
    }
    // 单例复用（open → 已存在仅 show/raise）时：未恢复过保存位置才重新居中；
    // 质检修正——窗口销毁重建路径下原实现的 onVisibleChanged 无条件居中，
    // Component.onCompleted 刚恢复的 result_x/y 立即被覆盖，持久化形同虚设
    onVisibleChanged: {
        if (!visible)
            return
        if (!resultWindow.geometryRestored)
            resultWindow.centerOnScreen()
        resultWindow.startReveal()
    }
    onTargetScreenChanged: centerOnScreen()

    // ─────────────────────────── 动作 ───────────────────────────

    // 滚动 flicker + 金色落定：每次 drawn 变化重播（60ms 闪烁 animSecs 秒，可提前结束）
    property bool flickering: false
    property var flickerNames: []
    property int flickerIndex: 0
    // 悬念收口：本轮动画落定（或用户提前结束）后才允许播报。灵动通知不得先于
    // 揭晓出现——公告一眼泄底，滚动动画就失去悬念意义（参考插件
    // rollcall-result.qml 亦在 finish() 定格后才回调 backend.onPicked 播报）
    property bool announcePending: false

    function drawnNames() {
        const out = []
        for (let i = 0; i < resultWindow.drawn.length; i++) {
            const d = resultWindow.drawn[i]
            out.push((typeof d === "string") ? d : (d.name || ""))
        }
        return out
    }

    function startReveal() {
        if (resultWindow.drawn.length === 0) {
            resultWindow.flickering = false
            resultWindow.announcePending = false
            return
        }
        resultWindow.flickering = true
        resultWindow.announcePending = true
        resultWindow.flickerIndex = 0
        flickerTimer.restart()
        revealTimer.interval = resultWindow.animSecs * 1000
        revealTimer.restart()
    }
    function finishReveal() {
        flickerTimer.stop()
        revealTimer.stop()
        resultWindow.flickering = false
        if (!resultWindow.announcePending)
            return
        resultWindow.announcePending = false
        // 首次点名与"再点 N 名"共用本收口：每次定格各播报本轮结果
        if (RollCall)
            RollCall.announce(resultWindow.drawnNames())
    }

    // 卡片 TapHandler 的命中豁免：点按落在控制件（按钮行/缩放手柄）上时返回 true。
    // 原因见 resultCard TapHandler 注释；pos 为卡片坐标系（eventPoint.position）
    function tapOnControl(posInCard) {
        const controls = [controlsRow, resizeHandle]
        for (let i = 0; i < controls.length; i++) {
            const c = controls[i]
            if (!c || !c.visible)
                continue
            const origin = c.mapToItem(resultCard, 0, 0)
            if (posInCard.x >= origin.x && posInCard.x < origin.x + c.width
                    && posInCard.y >= origin.y && posInCard.y < origin.y + c.height)
                return true
        }
        return false
    }
    Timer {
        id: flickerTimer
        interval: 60
        repeat: true
        onTriggered: {
            // 闪烁池：名单全体（无名单时用已抽结果兜底，避免空闪）
            const pool = resultWindow.roster.length > 0 ? resultWindow.roster : resultWindow.drawn
            if (pool.length === 0) return
            const show = []
            for (let i = 0; i < resultWindow.drawn.length; i++) {
                // 每格只取一次随机数（质检修正：原实现 name 取兜底时二次随机，
                // 兜底分支会闪到另一个人）
                const pick = pool[Math.floor(Math.random() * pool.length)]
                show.push((typeof pick === "string") ? pick : (pick.name || pick))
            }
            resultWindow.flickerNames = show
        }
    }
    Timer {
        id: revealTimer
        repeat: false
        onTriggered: resultWindow.finishReveal()
    }
    // 每次 draw 都必须重播动画，故以服务端 drawCompleted 信号为准而非 drawn
    // 值变化：single 模式连续抽到同一个人时 lastDraw 值相等，值比较可能不触发
    // onDrawnChanged（"再点"看上去毫无反应）。面板按钮与悬浮窗两条路径都走
    // RollCall.draw → 本信号
    Connections {
        target: RollCall
        function onDrawCompleted(result) {
            resultWindow.startReveal()
        }
    }

    function persistGeometry() {
        // 逐键锁定检查（质检修正：原实现误检 button_x——锁定点名按钮位置会
        // 连带禁掉结果窗几何持久化，而锁定 result_* 键反而起不到阻止作用）
        if (!Configs.isKeyLocked("extensions.roll_call.result_x"))
            Configs.set("extensions.roll_call.result_x", Math.round(resultWindow.x))
        if (!Configs.isKeyLocked("extensions.roll_call.result_y"))
            Configs.set("extensions.roll_call.result_y", Math.round(resultWindow.y))
        if (!Configs.isKeyLocked("extensions.roll_call.result_w"))
            Configs.set("extensions.roll_call.result_w", Math.round(resultWindow.width))
        if (!Configs.isKeyLocked("extensions.roll_call.result_h"))
            Configs.set("extensions.roll_call.result_h", Math.round(resultWindow.height))
    }

    function rollMore(count) {
        if (RollCall)
            RollCall.draw(count)
        // session 模式下排除逻辑在服务端：已点过的人不会再出现在 drawn 里；
        // 全部点完后 drawn 为空 → 卡片内显示引导文案
    }

    // 关闭：清会话排除名单（§6 C4："结果窗口关闭时清空会话"），再走管理器
    // 的销毁路径（0ms singleShot 释放，QML 回调栈内不拆对象树）
    function closeAndReset() {
        resultWindow.persistGeometry()
        if (RollCall)
            RollCall.clearSession()
        WindowManager.closeRollCallResult()
    }

    // 结果卡片：不透明深底浮在透明窗口上，无边框窗口的"窗口感"由这张卡片给
    // 可拖动（整卡拖拽移动窗口，松手持久化几何）；点按提前结束 flicker
    Rectangle {
        id: resultCard
        anchors.fill: parent
        anchors.margins: 12
        radius: 20
        color: Qt.rgba(0.09, 0.10, 0.15, 0.96)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.10)

        DragHandler {
            id: cardDrag
            target: null
            property real startWX: 0
            property real startWY: 0
            onActiveChanged: {
                if (active) {
                    startWX = resultWindow.x
                    startWY = resultWindow.y
                } else {
                    resultWindow.persistGeometry()
                }
            }
            onTranslationChanged: {
                if (!active) return
                resultWindow.x = startWX + translation.x
                resultWindow.y = startWY + translation.y
            }
        }
        TapHandler {
            // 点按卡片提前结束滚动（仅 flicker 中消费）。注意：子按钮的 TapHandler
            // 与本处理器会同时收到同一次点按（Qt 指针处理器沿祖先链分发，子件不会
            // 截断父件），点「再点 N 名」时按钮路径 draw→startReveal 刚点亮的滚动
            // 会被同一个抬手掐灭；故按钮行/缩放手柄范围内的点按在此跳过
            onTapped: function (eventPoint) {
                if (!resultWindow.flickering)
                    return
                if (resultWindow.tapOnControl(eventPoint.position))
                    return
                resultWindow.finishReveal()
            }
        }

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

            // 抽中名单：flicker 中显示滚动名，落定后金色 #FFE08A 大字；1~3 人纵向排开
            Repeater {
                model: resultWindow.flickering ? resultWindow.flickerNames : resultWindow.drawn

                delegate: Text {
                    required property var modelData
                    required property int index
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: (typeof modelData === "string") ? modelData : (modelData.name || "")
                    color: resultWindow.flickering ? "white" : "#FFE08A"
                    font.pixelSize: resultWindow.flickering ? 40 : 52
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
                id: controlsRow
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 10

                MiniButton { label: qsTr("再点 1 名"); onClicked: resultWindow.rollMore(1) }
                MiniButton { label: qsTr("再点 2 名"); onClicked: resultWindow.rollMore(2) }
                MiniButton { label: qsTr("再点 3 名"); onClicked: resultWindow.rollMore(3) }
                MiniButton { label: resultWindow.flickering ? qsTr("停止") : qsTr("关闭")
                             muted: true
                             onClicked: resultWindow.flickering ? resultWindow.finishReveal()
                                                               : resultWindow.closeAndReset() }
            }
        }

        // 右下 18px 缩放手柄：拖拽改窗口宽高，松手持久化
        Rectangle {
            id: resizeHandle
            width: 18; height: 18
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 4
            radius: 4
            color: resizeHover.hovered ? Qt.rgba(1, 1, 1, 0.35) : Qt.rgba(1, 1, 1, 0.15)
            HoverHandler { id: resizeHover }
            DragHandler {
                id: resizeDrag
                target: null
                property real startW: 0
                property real startH: 0
                onActiveChanged: {
                    if (active) {
                        startW = resultWindow.width
                        startH = resultWindow.height
                    } else {
                        resultWindow.width = Math.max(320, resultWindow.width)
                        resultWindow.height = Math.max(240, resultWindow.height)
                        resultWindow.persistGeometry()
                    }
                }
                onTranslationChanged: {
                    if (!active) return
                    resultWindow.width = Math.max(320, startW + translation.x)
                    resultWindow.height = Math.max(240, startH + translation.y)
                }
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
