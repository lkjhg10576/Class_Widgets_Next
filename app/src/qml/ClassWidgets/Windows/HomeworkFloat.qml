import QtQuick
import QtQuick.Window as QQW
import QtQuick.Controls
import QtQuick.Layouts
import RinUI
import ClassWidgets.Components
import ClassWidgets.Theme 1.0

// 当日作业浮窗（扩展 classwidgets.ext.homework，F2/F6/F7）：
// 独立 Frameless|StaysOnTop|Tool 窗口，默认屏幕右侧垂直居中；标题栏拖动 +
// 右下 18px 缩放手柄（min 240x200，钳屏），几何持久化到 extensions.homework.window_*。
// 注意：拖动收窄到标题栏而非整卡——卡片主体是 ListView（Flickable），祖先级
// DragHandler 会与列表滚动手势抢指针（整卡拖动会让列表滚不动）。
// 外观外壳用 ClassWidgets.Theme 的 Widget（自带亮暗与主题覆盖；hover 变暗与
// 高度动画是顶栏小组件的交互语义，浮窗内经 opacity:1 显式关闭）。
// 内容为标题栏 + 当日作业 ListView + 底部「+」行；选中行浮现编辑/删除二键，
// 编辑/删除分别经 HomeworkEditDialog 与确认对话框完成。
// 窗口生命周期照 RollCallFloat/RollCallResult：onClosing 拦截转 WindowManager
// 的 close 槽（hide + 0ms 延迟销毁，重开重建）。
// 字号注意：本文件 import ClassWidgets.Theme 1.0 使裸 Text 解析为 Theme 的
// Text（其 font.pixelSize 绑定引用了未声明的 px，会刷 ReferenceError），因此
// 每个 Text 都必须显式给 font.pixelSize（与 FloatingWidget.qml 的既有约定一致）。
QQW.Window {
    id: floatWindow
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool
    color: "transparent"

    onClosing: function(event) {
        event.accepted = false
        floatWindow.persistGeometry()
        WindowManager.closeHomeworkFloat()
    }

    // ─────────────────────────── 配置与状态 ───────────────────────────
    // Configs/Extensions/AppCentral 均判空：应用退出拆引擎时上下文属性先于
    // QML 对象销毁，绑定会再求值一次（与 HomeworkTrigger 同款纪律）
    readonly property var hwCfg: {
        const ext = (Configs && Configs.data) ? Configs.data.extensions : null
        return (ext && ext.homework) ? ext.homework : {}
    }
    // locked=true（默认）：禁拖动 + 隐藏缩放手柄；isKeyLocked 在 persistGeometry
    // 里逐键双检（与 RollCallResult.persistGeometry 同口径）
    readonly property bool locked: hwCfg.locked !== false
    // isEnabled() 是 Q_INVOKABLE，绑定内显式读 Extensions.extensions 建立依赖
    readonly property bool extEnabled: {
        if (!Extensions)
            return false
        Extensions.extensions
        return Extensions.isEnabled("classwidgets.ext.homework")
    }
    readonly property int edgeMargin: 24
    readonly property var todayItems: Homework ? Homework.todayItems : []
    // 删除确认的条目 id（对话框打开期间暂存）
    property string deleteTargetId: ""

    // ─────────────────────────── 屏幕与几何 ───────────────────────────
    // 选屏与 MainInterface/RollCallFloat 同源：preferences.display 匹配
    // Qt.application.screens，未命中回退第一块屏
    property string screenName: Configs.data.preferences.display
        || (Qt.application.screens.length > 0 ? Qt.application.screens[0].name : "")
    property var targetScreen: {
        for (let s of Qt.application.screens) {
            if (s.name === screenName)
                return s
        }
        return Qt.application.screens.length > 0 ? Qt.application.screens[0] : null
    }

    width: 320
    height: 400
    property bool geometryRestored: false

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

    function centerOnScreen() {
        const s = floatWindow.targetScreen
        if (!s)
            return
        // 首启：所选屏幕右侧垂直居中（FloatingWidgetContainer.ensurePosition 取值）
        floatWindow.x = s.virtualX + s.width - floatWindow.width - floatWindow.edgeMargin
        floatWindow.y = s.virtualY + Math.max(0, (s.height - floatWindow.height) / 2)
    }

    function clampToScreen(px, py) {
        const s = floatWindow.targetScreen
        if (!s)
            return { x: px, y: py }
        // 统一用 edgeMargin（与 centerOnScreen/首启一致）；小屏放不下时 Math.max
        // 兜底贴左上，绝不把窗口推到屏幕外
        return {
            x: Math.max(s.virtualX + floatWindow.edgeMargin,
                        Math.min(s.virtualX + s.width - floatWindow.width
                                                     - floatWindow.edgeMargin, px)),
            y: Math.max(s.virtualY + floatWindow.edgeMargin,
                        Math.min(s.virtualY + s.height - floatWindow.height
                                                      - floatWindow.edgeMargin, py))
        }
    }

    // 屏幕几何变化（改分辨率/系统缩放）后把窗口收回可视范围
    function reclampToScreen() {
        if (!floatWindow.visible)
            return
        const clamped = floatWindow.clampToScreen(floatWindow.x, floatWindow.y)
        floatWindow.x = clamped.x
        floatWindow.y = clamped.y
    }

    Component.onCompleted: {
        const hw = Configs.data.extensions ? Configs.data.extensions.homework : null
        const s = floatWindow.targetScreen
        if (hw && hw.window_w > 0 && hw.window_h > 0 && s) {
            // 恢复尺寸并按屏幕可用范围收口（下限与缩放手柄一致 240x200）
            floatWindow.width = Math.max(240, Math.min(hw.window_w, s.width - 16))
            floatWindow.height = Math.max(200, Math.min(hw.window_h, s.height - 16))
        }
        if (hw && hw.window_x !== undefined && hw.window_x !== null
                && hw.window_y !== undefined && hw.window_y !== null
                && floatWindow.pointOnAnyScreen(hw.window_x, hw.window_y)) {
            floatWindow.x = hw.window_x
            floatWindow.y = hw.window_y
            floatWindow.geometryRestored = true
            // 恢复的位置只验证了「落在某块屏上」，未保证完整可见（窗口可能比
            // 上次会话大），按当前尺寸再钳一次
            const clamped = floatWindow.clampToScreen(floatWindow.x, floatWindow.y)
            floatWindow.x = clamped.x
            floatWindow.y = clamped.y
        } else {
            floatWindow.centerOnScreen()
        }
    }
    // 单例复用（open → show/raise）：未恢复过位置才重新居中。数据刷新不在这里做：
    // HomeworkService 已随写操作与跨天心跳自行 reload 并发 itemsChanged，
    // 每次显示都重读会整表重建 ListView 并清掉用户上次的行选中态
    onVisibleChanged: {
        if (!visible)
            return
        if (!floatWindow.geometryRestored)
            floatWindow.centerOnScreen()
        else
            floatWindow.reclampToScreen()
    }
    onTargetScreenChanged: if (!floatWindow.geometryRestored)
                               floatWindow.centerOnScreen()

    // 分辨率/系统缩放变化的收口：QML 侧 Qt.application.screens 给出的是
    // QQuickScreenInfo 包装对象，观察不到 QScreen::geometryChanged（实测
    // Connections 会报 "no signal matches"），故在每次显示与选屏切换时重钳位，
    // 不做常驻几何监听

    function persistGeometry() {
        if (!Configs)
            return
        // 逐键锁定检查（同 RollCallResult：锁 window_x 不应连带禁掉尺寸持久化）
        if (!Configs.isKeyLocked("extensions.homework.window_x"))
            Configs.set("extensions.homework.window_x", Math.round(floatWindow.x))
        if (!Configs.isKeyLocked("extensions.homework.window_y"))
            Configs.set("extensions.homework.window_y", Math.round(floatWindow.y))
        if (!Configs.isKeyLocked("extensions.homework.window_w"))
            Configs.set("extensions.homework.window_w", Math.round(floatWindow.width))
        if (!Configs.isKeyLocked("extensions.homework.window_h"))
            Configs.set("extensions.homework.window_h", Math.round(floatWindow.height))
    }

    // ─────────────────────────── 数据辅助 ───────────────────────────
    function hmToMinutes(text) {
        if (!text)
            return -1
        const parts = String(text).split(":")
        if (parts.length < 2)
            return -1
        const h = Number(parts[0])
        const m = Number(parts[1])
        if (!isFinite(h) || !isFinite(m))
            return -1
        return h * 60 + m
    }

    function subjectName(id) {
        if (!id)
            return ""
        const runtime = AppCentral ? AppCentral.scheduleRuntime : null
        const subjects = runtime ? (runtime.subjects || []) : []
        for (let i = 0; i < subjects.length; ++i) {
            if (subjects[i].id === id)
                return subjects[i].name || subjects[i].id
        }
        return ""
    }

    // 优先级仅整行字体变色（none=主题默认色；F6 定稿，不做色盲模式）
    function priorityColor(priority) {
        switch (priority) {
        case "orange":
            return "#E67E00"
        case "blue":
            return "#1E90FF"
        case "green":
            return "#2E9E5B"
        default:
            return Theme.currentTheme.colors.textColor
        }
    }

    function escapeHtml(text) {
        // \r 一并剥掉：Windows 换行（\r\n）在 RichText 里会多渲染一行空白
        return String(text).replace(/\r/g, "")
                           .replace(/&/g, "&amp;")
                           .replace(/</g, "&lt;")
                           .replace(/>/g, "&gt;")
    }

    // "<b>科目：</b>内容"；未指定科目时仅内容（科目名同样转义）
    function itemRichText(item) {
        const content = floatWindow.escapeHtml(item.content || "")
        const subject = floatWindow.subjectName(item.subjectId)
        if (!subject)
            return content
        return "<b>" + floatWindow.escapeHtml(subject) + "：</b>" + content
    }

    function itemAt(index) {
        if (index < 0 || index >= floatWindow.todayItems.length)
            return null
        return floatWindow.todayItems[index]
    }

    // 上节课科目（F5 预填）：currentDayEntries 过滤 type==class && end<=now，
    // 取 end 最大者（SchedulePeekBar 同款现算，时间基准叠加 time_offset）
    function lastClassSubjectId() {
        const runtime = AppCentral ? AppCentral.scheduleRuntime : null
        const entries = runtime ? (runtime.currentDayEntries || []) : []
        const nowMinutes = floatWindow.currentMinutes()
        let best = null
        let bestEnd = -1
        for (let i = 0; i < entries.length; ++i) {
            const entry = entries[i]
            if (entry.type !== "class")
                continue
            const end = floatWindow.hmToMinutes(entry.endTime)
            if (end < 0 || end > nowMinutes)
                continue
            if (end > bestEnd) {
                bestEnd = end
                best = entry
            }
        }
        return best ? String(best.subjectId || "") : ""
    }

    function currentMinutes() {
        const runtime = AppCentral ? AppCentral.scheduleRuntime : null
        if (!runtime)
            return -1
        const minute = floatWindow.hmToMinutes(runtime.currentTime)
        if (minute < 0)
            return -1
        return minute + Math.round((runtime.timeOffset || 0) / 60)
    }

    // ─────────────────────────── 交互动作 ───────────────────────────
    function openAddDialog() {
        editDialog.openFor("", floatWindow.lastClassSubjectId(), "", "none")
    }

    function openEditDialog(index) {
        const item = floatWindow.itemAt(index)
        if (!item)
            return
        editDialog.openFor(String(item.id || ""), String(item.subjectId || ""),
                           String(item.content || ""), String(item.priority || "none"))
    }

    function confirmDelete(index) {
        const item = floatWindow.itemAt(index)
        if (!item)
            return
        floatWindow.deleteTargetId = String(item.id || "")
        deleteDialog.open()
    }

    // ─────────────────────────── 外观 ───────────────────────────
    Item {
        id: rootItem
        anchors.fill: parent

        // 外壳：主题 Widget（亮暗自适应 + 主题覆盖），内容放进其 content 区。
        // opacity 显式钉 1：Widget 默认 hover 整卡变暗（顶栏小组件的点击暗示，
        // 对编辑浮窗只损失可读性）；height 动画仅作用于直接写 height，锚定布局不受影响
        Widget {
            id: card
            anchors.fill: parent
            opacity: 1

            ColumnLayout {
                anchors.fill: parent
                spacing: 6

                // 标题栏：标题 / 锁标记 / 日期 / 关闭。整窗唯一的拖动热区
                //（DragHandler 收窄在此，避开列表的滚动手势，见文件头注释）
                RowLayout {
                    id: titleBar
                    Layout.fillWidth: true
                    spacing: 6

                    DragHandler {
                        id: titleDrag
                        target: null
                        enabled: !floatWindow.locked && !resizeDrag.active
                        property real startWX: 0
                        property real startWY: 0
                        property real movedTotal: 0
                        onActiveChanged: {
                            if (active) {
                                startWX = floatWindow.x
                                startWY = floatWindow.y
                                movedTotal = 0
                            } else if (movedTotal > 4) {
                                // 有真实位移才持久化；纯点击（选行/关按钮）不写 4 个配置键
                                floatWindow.persistGeometry()
                            }
                        }
                        onTranslationChanged: {
                            if (!active)
                                return
                            movedTotal = Math.max(movedTotal,
                                                  Math.abs(translation.x)
                                                      + Math.abs(translation.y))
                            const clamped = floatWindow.clampToScreen(
                                startWX + translation.x, startWY + translation.y)
                            floatWindow.x = clamped.x
                            floatWindow.y = clamped.y
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("当日作业")
                        font.pixelSize: 17
                        font.bold: true
                        elide: Text.ElideRight
                        color: Theme.currentTheme.colors.textColor
                    }

                    Icon {
                        id: lockIcon
                        visible: floatWindow.locked
                        name: "ic_fluent_lock_closed_20_regular"
                        size: 14
                        color: Theme.currentTheme.colors.textSecondaryColor

                        HoverHandler { id: lockHover }
                        ToolTip.visible: lockHover.hovered
                        ToolTip.text: qsTr("已锁定位置与大小，可在扩展设置中解锁")
                    }

                    Text {
                        text: Homework ? Homework.todayDate : ""
                        font.pixelSize: 12
                        color: Theme.currentTheme.colors.textSecondaryColor
                    }

                    ToolButton {
                        icon.name: "ic_fluent_dismiss_20_regular"
                        flat: true
                        implicitWidth: 30
                        implicitHeight: 30
                        onClicked: {
                            floatWindow.persistGeometry()
                            WindowManager.closeHomeworkFloat()
                        }
                    }
                }

                // 列表区（滚动/空状态/选中栏都在这里；footer 独立在列表之下，
                // 恒贴窗口底部——放 ListView.footer 会在空列表时顶到第一行）
                Item {
                    id: listArea
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ListView {
                        id: itemList
                        anchors.fill: parent
                        clip: true
                        spacing: 2
                        model: floatWindow.todayItems
                        currentIndex: -1
                        onModelChanged: currentIndex = -1

                        delegate: Rectangle {
                            id: itemDelegate
                            required property var modelData
                            required property int index

                            width: itemList.width
                            height: Math.max(34, delegateText.implicitHeight + 14)
                            radius: 8
                            color: itemList.currentIndex === index
                                   ? (Theme.isDark() ? Qt.rgba(1, 1, 1, 0.14)
                                                     : Qt.rgba(0, 0, 0, 0.08))
                                   : "transparent"
                            Behavior on color { ColorAnimation { duration: 100 } }

                            Text {
                                id: delegateText
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                textFormat: Text.RichText
                                wrapMode: Text.Wrap
                                font.pixelSize: 14 // Theme.Text 遮蔽 QtQuick.Text，必须显式给字号
                                text: floatWindow.itemRichText(itemDelegate.modelData)
                                color: floatWindow.priorityColor(itemDelegate.modelData.priority)
                            }

                            TapHandler {
                                onTapped: itemList.currentIndex =
                                    (itemList.currentIndex === itemDelegate.index ? -1
                                                                                 : itemDelegate.index)
                            }
                        }
                    }

                    // 空状态提示（F6）：ListView 的直接子项会进 contentData 跟着
                    // 内容滚动，作为覆盖层挂在列表之上才稳定居顶
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.top
                        anchors.topMargin: 18
                        width: Math.max(0, parent.width - 24)
                        visible: itemList.count === 0
                        text: qsTr("今日还没有作业，点击“+”添加")
                        wrapMode: Text.Wrap
                        horizontalAlignment: Text.AlignHCenter
                        font.pixelSize: 13
                        color: Theme.currentTheme.colors.textSecondaryColor
                    }

                    // 选中后浮现编辑/删除二键（贴列表右下，盖住末行属预期遮罩；
                    // 与底部「+」行分属两块区域，窄窗不再互相侵入）（F7）
                    Rectangle {
                        id: selectionBar
                        visible: itemList.currentIndex >= 0
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.rightMargin: 4
                        anchors.bottomMargin: 4
                        width: selectionRow.implicitWidth + 16
                        height: 38
                        radius: 10
                        color: Theme.isDark() ? Qt.rgba(0.13, 0.13, 0.16, 0.96)
                                              : Qt.rgba(1, 1, 1, 0.96)
                        border.width: 1
                        border.color: Theme.isDark() ? Qt.rgba(1, 1, 1, 0.12)
                                                     : Qt.rgba(0, 0, 0, 0.08)

                        Row {
                            id: selectionRow
                            anchors.centerIn: parent
                            spacing: 2

                            ToolButton {
                                icon.name: "ic_fluent_edit_20_regular"
                                flat: true
                                implicitWidth: 32
                                implicitHeight: 32
                                onClicked: floatWindow.openEditDialog(itemList.currentIndex)
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("编辑")
                            }
                            ToolButton {
                                icon.name: "ic_fluent_delete_20_regular"
                                flat: true
                                implicitWidth: 32
                                implicitHeight: 32
                                onClicked: floatWindow.confirmDelete(itemList.currentIndex)
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("删除")
                            }
                        }
                    }
                }

                // 底部「+」行：恒贴窗口底部（空列表时即列表区之下的第一可点行）
                Rectangle {
                    id: footerAdd
                    Layout.fillWidth: true
                    Layout.preferredHeight: 38
                    radius: 8
                    color: addHover.hovered
                           ? (Theme.isDark() ? Qt.rgba(1, 1, 1, 0.10)
                                             : Qt.rgba(0, 0, 0, 0.05))
                           : "transparent"
                    Behavior on color { ColorAnimation { duration: 100 } }

                    RowLayout {
                        anchors.centerIn: parent
                        spacing: 4
                        Icon {
                            name: "ic_fluent_add_20_regular"
                            size: 16
                            color: Theme.currentTheme.colors.textSecondaryColor
                        }
                        Text {
                            text: qsTr("添加作业")
                            font.pixelSize: 14
                            color: Theme.currentTheme.colors.textSecondaryColor
                        }
                    }

                    HoverHandler { id: addHover }
                    TapHandler { onTapped: floatWindow.openAddDialog() }
                }
            }
        }

        // 拖动已收窄到标题栏（titleDrag）：整卡 DragHandler 会与 ListView 的
        // 滚动手势抢指针，导致列表滚不动（见文件头注释）

        // 右下 18px 缩放手柄：locked / 选中编辑态隐藏（F4/F7）
        Rectangle {
            id: resizeHandle
            visible: !floatWindow.locked && itemList.currentIndex < 0
                     && !editDialog.visible && !deleteDialog.visible
            width: 18
            height: 18
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 4
            radius: 4
            color: resizeHover.hovered ? Qt.rgba(1, 1, 1, 0.35) : Qt.rgba(0.5, 0.5, 0.5, 0.25)

            HoverHandler { id: resizeHover }

            DragHandler {
                id: resizeDrag
                target: null
                property real startW: 0
                property real startH: 0
                property real movedTotal: 0
                onActiveChanged: {
                    if (active) {
                        startW = floatWindow.width
                        startH = floatWindow.height
                        movedTotal = 0
                    } else if (movedTotal > 4) {
                        floatWindow.persistGeometry()
                    }
                }
                onTranslationChanged: {
                    if (!active)
                        return
                    movedTotal = Math.max(movedTotal, Math.abs(translation.x)
                                                              + Math.abs(translation.y))
                    const s = floatWindow.targetScreen
                    const maxW = s ? s.width - 16 : 4096
                    const maxH = s ? s.height - 16 : 4096
                    floatWindow.width = Math.max(240,
                                                 Math.min(maxW, startW + translation.x))
                    floatWindow.height = Math.max(200,
                                                  Math.min(maxH, startH + translation.y))
                }
            }
        }
    }

    // ─────────────────────────── 对话框 ───────────────────────────
    // 编辑/新建：数据写入由对话框自身经 Homework 服务完成（Ok 仅在正文非空时可用）
    HomeworkEditDialog {
        id: editDialog
    }

    // 删除确认（文案定稿，照 PluginReplaceConfirmDialog 的 Ok|Cancel 骨架）
    Dialog {
        id: deleteDialog
        title: qsTr("删除作业")
        modal: true
        width: Math.min(340, Math.max(0, floatWindow.width - 24))

        // 正文必须挂进布局：RinUI.Dialog 的 contentItem 是 ColumnLayout，布局会用
        // implicitWidth 接管子项 width，原先的 `width: parent.width` 被覆盖；
        // 而 wrapMode: Text.Wrap 的 implicitWidth 是不换行的自然宽度，窄浮窗下整行
        // 正文冲出对话框右边界、被浮窗裁断（Layout.fillWidth 才是约束后的可用宽）
        ColumnLayout {
            Layout.fillWidth: true

            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: 14 // Theme.Text 遮蔽 QtQuick.Text，必须显式给字号
                text: qsTr("即将删除一条作业，此操作不可撤销，是否继续？")
            }
        }

        // footer 自绘（同 HomeworkEditDialog）：RinUI.DialogButtonBox 的 background
        // 带底色 + windowBorderColor 描边，叠在对话框自己的圆角边框内会多出一圈
        // 直角框线（顶部还有块方角补片），圆角与方角边框同时出现；这里只留按钮
        footer: DialogButtonBox {
            standardButtons: DialogButtonBox.Ok | DialogButtonBox.Cancel
            background: Item {} // 去掉底板：对白框本身已是底色

            onAccepted: deleteDialog.acceptDelete()
            onRejected: deleteDialog.close()
        }

        // 确定删除：走 Homework 服务落盘；取消不回写，下次 confirmDelete 会覆盖暂存 id
        function acceptDelete() {
            if (floatWindow.deleteTargetId && Homework)
                Homework.removeItem(floatWindow.deleteTargetId)
            floatWindow.deleteTargetId = ""
            itemList.currentIndex = -1
            deleteDialog.close()
        }
    }
}
