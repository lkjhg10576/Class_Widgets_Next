import QtQuick
import QtQuick.Controls
import QtQuick as QQ
import QtQuick.Controls as QQC
import QtQuick.Layouts
import QtQuick.Window as QQW
import RinUI
import ClassWidgets.Components

QQW.Window {
    id: root
    visible: true
    // 动态判断平台
    flags: {
        let result = Qt.FramelessWindowHint | Qt.Window | Qt.WindowStaysOnTopHint;

        if (Qt.platform.os === "osx" || Qt.platform.os === "macos") {  // 修复macOS窗口问题
            return result;
        } else {
            // Windows：小组件页面不会被alt+tab截获
            return  Qt.Tool | result;
        }
    }
    color: "transparent"

    property string screenName: Configs.data.preferences.display || Qt.application.screens[0].name
    property var screen: {
        for (let s of Qt.application.screens) {
            if (s.name === screenName)
                return s
        }
        return Qt.application.screens[0]
    }

    x: screen.virtualX + ((screen.width - width) / 2)  || 0
    y: screen.virtualY + ((screen.height - height) / 2) || 0
    width: screen.width
    height: screen.height

    property bool initialized: false
    property alias editMode: widgetsLoader.editMode
    property bool mouseHovered: false
    // 主窗口蒙版（WidgetsWindow::updateMask 只暴露小组件矩形）会把渲染在窗口 overlay
    // 屏幕中央的 RinUI Dialog（QQC2 Popup，无独立原生窗口）整块裁掉——弹窗其实开着，
    // 用户却看到"无弹窗 + 小组件被模态遮罩压暗"的假死。弹窗可见期间将本属性置真，
    // C++ 侧据此摘掉蒙版（与 menuVisible/editMode 同一处理路径）；visible 在开启动画
    // 起始即置位、退场动画结束后才复位，蒙版切换正好包住弹窗动画。
    property bool dialogOpen: rescheduleDayDialog.visible || switchScheduleDialog.visible
    onDialogOpenChanged: widgetsLoader.geometryChanged()
    property bool isFloatingMode: Configs.data.interactions.hide.state
        && (Configs.data.interactions.tapped_action === "floating_widget"
            || Configs.data.interactions.hide.action === "floating_widget")

    onMouseHoveredChanged: {
        root.flags = mouseHovered
            ? root.flags | Qt.WindowTransparentForInput
            : root.flags & ~Qt.WindowTransparentForInput
    }

    //background
    Rectangle {
        id: background
        anchors.fill: parent
        visible: editMode
        color: "black"
        opacity: editMode? 0.25 : 0
        Behavior on opacity {
            NumberAnimation {
                duration: 200
                easing.type: Easing.InOutQuad
            }
        }
    }

    Timer {
        id: initalizeTimer
        interval: 300
        running: true
        repeat: false
        onTriggered: root.initialized = true
    }

    MouseArea {
        anchors.fill: parent
        onClicked: {
            if (widgetsLoader.menuVisible) {
                widgetsLoader.menuVisible = false
            }
        }
    }

    // B4（托盘菜单扩展）：调休弹窗必须挂在常驻主窗口——RinUI Dialog 是 QQC2
    // Popup，渲染在所属窗口 overlay 内，挂在会 hide 的窗口下会随宿主一起消失。
    Connections {
        target: AppCentral
        function onTrayShortcutRequested(shortcutId) {
            if (shortcutId === "com.classwidgets.reschedule-day") {
                // 从托盘菜单触发时主窗口不在前台，先激活拿到键盘焦点（Esc/Enter 可用）
                root.raise()
                root.requestActivate()
                rescheduleDayDialog.open()
            }
        }
        function onTraySwitchScheduleRequested() {
            root.raise()
            root.requestActivate()
            switchScheduleDialog.open()
        }
    }

    // 托盘菜单"调休"入口的弹出界面（原 TrayPanel 宫格路径已随面板移除）
    RescheduleDayDialog {
        id: rescheduleDayDialog
        title: qsTr("Reschedule Day")
        width: Math.min(420, root.width * 0.9)

        ButtonGroup {
            id: buttonGroup
            exclusive: true
        }
    }

    // 托盘菜单"切换课程表"的弹出界面
    SwitchScheduleDialog {
        id: switchScheduleDialog
        width: Math.min(360, root.width * 0.9)
    }

    Watermark {
        x: widgetsLoader.x
        y: widgetsLoader.y + widgetsLoader.height / 3
        opacity: 0.2
        color: "gray"
        z: 999
    }

    WidgetsContainer {
        id: widgetsLoader
        objectName: "widgetsLoader"
        // 编辑按钮位于容器外层，必须高于 Watermark，避免被其覆盖
        z: editMode ? 1000 : 0

        // 坐标控制迁移到WidgetsContainer

        // 鼠标悬浮隐藏
        opacity: mouseHovered ? 0.25
            : editMode ? 1
            : hide ? 0.75 : 1

        Behavior on x { NumberAnimation { duration: 400 * root.initialized; easing.type: Easing.OutQuint } }
        Behavior on y { NumberAnimation { duration: 500 * root.initialized; easing.type: Easing.OutQuint } }

        TapHandler {
            id: hideTapHandler
            enabled: Configs.data.interactions.hide.clicked
            onTapped: {
                // 点击小组件：根据 tapped_action 决定隐藏或切换迷你模式
                if (Configs.data.interactions.tapped_action === "mini_mode") {
                    if (!Configs.isKeyLocked("preferences.mini_mode"))
                        Configs.set("preferences.mini_mode", !Configs.data.preferences.mini_mode)
                } else if (!Configs.isKeyLocked("interactions.hide.state")) {
                    Configs.set("interactions.hide.state", !Configs.data.interactions.hide.state)
                }
            }
        }

        signal geometryChanged()
        onXChanged: geometryChanged()
        onYChanged: geometryChanged()
        onWidthChanged: geometryChanged()
        onHeightChanged: geometryChanged()
        onEditModeChanged: geometryChanged()
        onMenuVisibleChanged: geometryChanged()
        onContentGeometryChanged: geometryChanged()
    }

    FloatingWidgetContainer {
        id: floatingWidgetContainer
        floatingMode: root.isFloatingMode
        screenWidth: root.width
        screenHeight: root.height

        onClicked: {
            if (!Configs.isKeyLocked("interactions.hide.state"))
                Configs.set("interactions.hide.state", false)
        }
    }

    // 当日作业扩展（classwidgets.ext.homework）：下课自动弹出右侧作业浮窗 +
    // 「作业布置」灵动通知的常驻触发器（F1/F3）。浮窗本身关闭即销毁，触发逻辑
    // 必须挂在主窗口；扩展开关关闭时组件内所有路径直接早退
    HomeworkTrigger {
        id: homeworkTrigger
    }

    Component.onCompleted: {
        updateLayer()
        // 应用当前主题的主题色
        var currentTheme = CWThemeManager.getThemeById(CWThemeManager.currentTheme)
        if (currentTheme && currentTheme.color) {
            // Theme.setThemeColor(currentTheme.color)
        } else {
            // origin #5CDCFF; 因为RinUI自带在暗色模式中的主题色补偿（这不夸夸（bushi），所以这里改成了更深的色调
            Theme.setThemeColor("#4099b2")
        }
    }


    Connections {
        target: Configs
        function onDataChanged() {
            updateLayer()
        }
    }

    Connections {
        target: CWThemeManager
        function onThemeChanged() {
            // 主题切换时应用主题色
            var currentTheme = CWThemeManager.getThemeById(CWThemeManager.currentTheme)
            if (currentTheme && currentTheme.color) {
                Theme.setThemeColor(currentTheme.color)
            }
        }
    }

    function updateLayer() {
        switch (Configs.data.preferences.widgets_layer) {
            case "top":
                root.flags &= ~Qt.WindowStaysOnBottomHint
                root.flags |= Qt.WindowStaysOnTopHint
                break
            case "bottom":
                root.flags &= ~Qt.WindowStaysOnTopHint
                root.flags |= Qt.WindowStaysOnBottomHint
                break
        }
    }
}
