import QtQuick
import QtQuick.Window as QQW

// 随机点名悬浮按钮窗（阶段 C3，extensions-feature-plan §6）。
// 独立 Window 绕开主窗口原生蒙版（§2：主界面蒙版只放行 widgetsFlow/浮窗容器，
// 其余元素不可见不可点），协作方式照抄 Windows/Settings.qml 与
// Components/dialogs/ClassSwapDialog.qml：根窗口不写 visible，由
// WindowManager.openRollCallFloat() show/raise；onClosing 拦截转 close 槽，
// 真正销毁走 AppWindowManager.releaseWindow 的 0ms 延迟释放。
QQW.Window {
    id: floatWindow
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool
    color: "transparent"

    onClosing: function(event) {
        // 受管窗口统一语义（Settings.qml:17-20 先例）：拒绝原生关闭路径，
        // 交还 WindowManager 做 hide + 延迟释放
        event.accepted = false
        WindowManager.closeRollCallFloat()
    }

    // ─────────────────────────── 几何与状态 ───────────────────────────
    // four-plugins C 一期：尺寸/样式/点击隐藏/课时隐藏全部经 extensions.roll_call.* 绑定
    // （ConfigStore 已钳位：w 40-160/h 30-100/animation 1-10/notify 2-15）。
    readonly property var rollCfg: (Configs.data.extensions && Configs.data.extensions.roll_call)
        ? Configs.data.extensions.roll_call : {}
    readonly property real buttonW: Math.min(160, Math.max(40, rollCfg.button_w || 60))
    readonly property real buttonH: Math.min(100, Math.max(30, rollCfg.button_h || 60))
    readonly property bool solidStyle: rollCfg.float_mode === "solid"
    readonly property bool clickHide: !!rollCfg.click_hide
    // （notify_duration 的停留时长与钳位已随播报还原一并移入 RollCallService::
    // announce，QML 侧不再需要本地副本）
    readonly property real buttonSize: 60
    readonly property real panelWidth: 340
    readonly property real edgeMargin: 24

    property bool panelOpen: false
    // 展开方向：点击展开瞬间按「按钮中心在屏幕左/右半边」定（右半边向左展开），
    // 面板开着期间拖拽会先收起面板，因此展开态与拖动不会同时改 x
    property bool expandLeft: false

    // 按钮左上角在所选屏幕本地坐标系的位置；持久化到
    // extensions.roll_call.button_x/y（-1 = 从未拖动 → 默认落在屏幕右上角）。
    // 屏幕选择照 MainInterface.qml:27-34：遍历 Qt.application.screens 匹配
    // preferences.display，未命中回退第一块屏
    property string screenName: Configs.data.preferences.display || Qt.application.screens[0].name
    property var targetScreen: {
        for (let s of Qt.application.screens) {
            if (s.name === screenName)
                return s
        }
        return Qt.application.screens[0]
    }
    property real buttonX: 0
    property real buttonY: 0
    property bool positionInitialized: false

    // 名单（空名单 → 面板内提示导入，不崩溃）；绑定 Configs.data，编辑即时联动
    readonly property var roster: (Configs.data.extensions
                                   && Configs.data.extensions.roll_call
                                   && Configs.data.extensions.roll_call.names) || []

    width: floatWindow.buttonW + (floatWindow.panelOpen ? floatWindow.panelWidth : 0)
    height: Math.max(floatWindow.buttonH, floatWindow.buttonSize)
    x: floatWindow.targetScreen
       ? floatWindow.targetScreen.virtualX + floatWindow.buttonX
         - (floatWindow.panelOpen && floatWindow.expandLeft ? floatWindow.panelWidth : 0)
       : 0
    y: floatWindow.targetScreen ? floatWindow.targetScreen.virtualY + floatWindow.buttonY : 0

    // ─────────────────────────── 位置管理（照 FloatingWidgetContainer 的
    // clampToBounds/ensurePosition/reconcilePosition/persistPosition 四件套，
    // 坐标系从"主窗口内 Item"换成"屏幕本地坐标 + Window.x/y"）───────────────────────────

    function clampX(value) {
        const s = floatWindow.targetScreen
        if (!s)
            return value
        return Math.max(floatWindow.edgeMargin,
                        Math.min(s.width - floatWindow.buttonW - floatWindow.edgeMargin, value))
    }

    function clampY(value) {
        const s = floatWindow.targetScreen
        if (!s)
            return value
        return Math.max(floatWindow.edgeMargin,
                        Math.min(s.height - floatWindow.buttonH - floatWindow.edgeMargin, value))
    }

    function ensurePosition() {
        if (floatWindow.positionInitialized || !floatWindow.targetScreen)
            return
        const rc = Configs.data.extensions.roll_call
        const savedX = rc && rc.button_x != null ? rc.button_x : -1
        const savedY = rc && rc.button_y != null ? rc.button_y : -1
        // -1 = 从未拖过：首显落所选屏幕右上角（产品定义，§1.3）
        floatWindow.buttonX = floatWindow.clampX(savedX >= 0 ? savedX
                                                              : floatWindow.targetScreen.width - floatWindow.buttonW - floatWindow.edgeMargin)
        floatWindow.buttonY = floatWindow.clampY(savedY >= 0 ? savedY : floatWindow.edgeMargin)
        floatWindow.positionInitialized = true
    }

    // 屏幕/分辨率变化重新钳位（reconcilePosition 先例）：targetScreen 绑定依赖
    // Qt.application.screens，列表变化即重估并触发下方 onTargetScreenChanged
    function reconcilePosition() {
        if (!floatWindow.positionInitialized)
            return
        floatWindow.buttonX = floatWindow.clampX(floatWindow.buttonX)
        floatWindow.buttonY = floatWindow.clampY(floatWindow.buttonY)
    }

    onTargetScreenChanged: floatWindow.reconcilePosition()

    function persistPosition() {
        // 锁定检查照 FloatingWidgetContainer.persistPosition:104-107
        if (!Configs.isKeyLocked("extensions.roll_call.button_x"))
            Configs.set("extensions.roll_call.button_x", Math.round(floatWindow.buttonX))
        if (!Configs.isKeyLocked("extensions.roll_call.button_y"))
            Configs.set("extensions.roll_call.button_y", Math.round(floatWindow.buttonY))
    }

    // ─────────────────────────── 交互 ───────────────────────────

    function togglePanel() {
        if (!floatWindow.panelOpen && floatWindow.targetScreen) {
            // 展开方向：按钮中心在屏幕右半边 → 向左展开；左半边 → 向右展开
            floatWindow.expandLeft =
                (floatWindow.buttonX + floatWindow.buttonSize / 2)
                > floatWindow.targetScreen.width / 2
        }
        floatWindow.panelOpen = !floatWindow.panelOpen
    }

    function rollCall(count) {
        floatWindow.panelOpen = false
        if (!RollCall)
            return
        // 播报（RollCall.announce）不在此处：点名结果由结果窗在滚动动画定格后
        // 统一播报，否则灵动通知先于揭晓弹出、"悬念"被提前泄底。窗口流程照旧：
        // 开结果窗 → click_hide 时隐藏悬浮按钮
        RollCall.draw(count)
        // 结果窗口无条件打开（质检修正：session 模式全员点完后原实现不开窗，
        // 而 clearSession 入口只存在结果窗内 → 按钮静默无反应的功能死锁；
        // 空结果由结果窗自行提示）
        WindowManager.openRollCallResult()
        // click_hide：点名后悬浮按钮一并隐藏（结果窗独立，不受影响）
        if (floatWindow.clickHide)
            WindowManager.closeRollCallFloat()
    }

    // 本节隐藏：上课期间悬浮窗隐藏，下课恢复；1h 兜底恢复（信号丢失时不永久消失）
    property bool classHidden: false
    Connections {
        target: AppCentral.scheduleRuntime
        function onCurrentStatusChanged() { floatWindow.updateClassHide() }
        function onCurrentEntryChanged() { floatWindow.updateClassHide() }
    }
    function updateClassHide() {
        const st = AppCentral.scheduleRuntime.currentStatus
        const hide = (st === "class")
        if (hide === floatWindow.classHidden) return
        floatWindow.classHidden = hide
        floatWindow.visible = !hide
        if (hide) classHideFallback.restart()
        else classHideFallback.stop()
    }
    Timer {
        id: classHideFallback
        interval: 3600000
        repeat: false
        onTriggered: {
            floatWindow.classHidden = false
            floatWindow.visible = true
        }
    }

    // 圆形点名按钮
    Rectangle {
        id: callButton
        width: floatWindow.buttonW
        height: floatWindow.buttonH
        radius: floatWindow.solidStyle ? 12 : floatWindow.buttonW / 2
        // 按钮始终保持在屏幕坐标 buttonX/buttonY：向右展开锚窗口左侧、
        // 向左展开时窗口整体左移了 panelWidth，按钮锚右侧
        x: floatWindow.panelOpen && floatWindow.expandLeft
           ? floatWindow.width - floatWindow.buttonW : 0
        y: 0
        // float=悬浮半透明 / solid=实心不透明（RollCall.qml 样式切换即时生效）
        color: floatWindow.solidStyle
               ? (hoverEngine.hovered ? Qt.rgba(0.10, 0.16, 0.32, 1) : Qt.rgba(0.07, 0.11, 0.24, 1))
               : (hoverEngine.hovered ? Qt.rgba(0.16, 0.24, 0.42, 0.92)
                                     : Qt.rgba(0.12, 0.16, 0.30, 0.72))
        scale: hoverEngine.hovered ? 1.06 : 1.0
        Behavior on color { ColorAnimation { duration: 120 } }
        Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }

        Text {
            anchors.centerIn: parent
            text: qsTr("点名")
            color: "white"
            font.pixelSize: 16
            font.bold: true
        }

        HoverHandler { id: hoverEngine }

        // 拖拽与点击的区分照抄 FloatingWidgetContainer 的组合拳（L290-385）：
        // DragHandler target:null 手动改窗口位置，位移 >8px 记 dragged，
        // 抬手置 suppressTap 并用 200ms Timer 压掉紧随的 tapped。
        // 未照抄惯性物理（FrameAnimation 弹簧回弹）：悬浮球是小尺寸单点交互，
        // 需求只要求钳位屏幕范围 + 位置持久化，拖动中直接硬钳位即可
        DragHandler {
            id: dragHandler
            target: null
            property real startX: 0
            property real startY: 0
            property bool dragged: false
            property bool suppressTap: false

            onActiveChanged: {
                if (active) {
                    startX = floatWindow.buttonX
                    startY = floatWindow.buttonY
                    dragged = false
                    suppressTap = false
                    // 拖动即收起面板：面板开着时窗口 x 依赖 expandLeft/
                    // panelOpen，与拖动改 buttonX 相互干扰，先回到纯按钮态
                    if (floatWindow.panelOpen)
                        floatWindow.panelOpen = false
                    return
                }
                suppressTap = dragged
                if (suppressTap)
                    suppressTapTimer.restart()
                if (dragged)
                    floatWindow.persistPosition()
            }

            onTranslationChanged: {
                if (!active)
                    return
                dragged = dragged || Math.abs(translation.x) > 8 || Math.abs(translation.y) > 8
                floatWindow.buttonX = floatWindow.clampX(startX + translation.x)
                floatWindow.buttonY = floatWindow.clampY(startY + translation.y)
            }
        }

        TapHandler {
            acceptedButtons: Qt.LeftButton
            onTapped: {
                if (!dragHandler.suppressTap)
                    floatWindow.togglePanel()
            }
        }
    }

    Timer {
        id: suppressTapTimer
        interval: 200
        repeat: false
        onTriggered: dragHandler.suppressTap = false
    }

    // 展开面板：点 1/2/3 名 + 取消（横向单行，嵌在与按钮同高的条状卡片里）
    Rectangle {
        id: panel
        visible: floatWindow.panelOpen
        width: floatWindow.panelWidth
        height: Math.max(floatWindow.buttonH, floatWindow.buttonSize)
        x: floatWindow.expandLeft ? 0 : floatWindow.buttonW
        y: 0
        radius: 14
        color: floatWindow.solidStyle ? Qt.rgba(0.07, 0.08, 0.12, 1)
                                      : Qt.rgba(0.10, 0.11, 0.16, 0.92)

        Row {
            anchors.centerIn: parent
            spacing: 8
            visible: floatWindow.roster.length > 0

            MiniButton { label: qsTr("点 1 名"); onClicked: floatWindow.rollCall(1) }
            MiniButton { label: qsTr("点 2 名"); onClicked: floatWindow.rollCall(2) }
            MiniButton { label: qsTr("点 3 名"); onClicked: floatWindow.rollCall(3) }
            MiniButton { label: qsTr("取消"); muted: true
                         onClicked: floatWindow.panelOpen = false }
        }

        // 名单为空的引导文案（任务要求：提示"请先在设置中导入名单"，不崩溃）
        Text {
            anchors.centerIn: parent
            visible: floatWindow.roster.length === 0
            text: qsTr("请先在设置中导入名单")
            color: "white"
            font.pixelSize: 14
        }
    }

    Component.onCompleted: {
        floatWindow.ensurePosition()
        // 上课中途开扩展/切屏：立即按当前状态评估本节隐藏（质检修正：原实现
        // 要等下一次 status/entry 变化才生效）
        floatWindow.updateClassHide()
    }

    // 面板/结果窗共用的扁平小按钮：自绘 Rectangle 而非 QQC/RinUI Button，
    // 悬浮窗只有这一处交互面，自绘避免为两个轻量窗口拉入整套控件样式
    component MiniButton: Rectangle {
        property string label: ""
        property bool muted: false
        signal clicked()

        width: Math.max(64, labelText.implicitWidth + 24)
        height: 40
        radius: 10
        color: muted ? (btnHover.hovered ? Qt.rgba(1, 1, 1, 0.18) : Qt.rgba(1, 1, 1, 0.10))
                     : (btnHover.hovered ? Qt.rgba(0.98, 0.98, 0.98, 1) : Qt.rgba(0.90, 0.90, 0.92, 1))
        Behavior on color { ColorAnimation { duration: 100 } }

        Text {
            id: labelText
            anchors.centerIn: parent
            text: parent.label
            color: parent.muted ? "white" : "#1b1b1f"
            font.pixelSize: 14
            font.bold: !parent.muted
        }

        HoverHandler { id: btnHover }
        TapHandler { onTapped: parent.clicked() }
    }
}
