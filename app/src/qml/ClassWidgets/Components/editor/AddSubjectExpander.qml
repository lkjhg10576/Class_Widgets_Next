import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RinUI


/**
 * AddSubjectExpander
 * ------------------
 * Schedule 编辑器「快捷添加学科」悬浮组件（基于 RinUI Expander，root 即 Expander）。
 * 由调用侧作为页面 root 的直接子项使用（不进任何 Layout），位置完全自管理。
 *
 *  行为：
 *  • Header 区域（不含右侧展开按钮）可 X / Y 双向拖动；内部 Flickable 滚动互不影响
 *  • 拖动用 Qt 标准 drag.target 机制：Qt 用全局光标追踪直接移动 root.x/y，
 *    无任何手动坐标换算 → 不存在反馈循环 / 坐标系漂移问题
 *  • 松手后 Y 用 OutBack 弹性动画吸附回「底部对齐」（动态值：按当前展开高度实时计算）
 *  • 展开/收起动画过程中 y 逐帧跟随高度变化，底部始终对齐；页面尺寸变化时自动重对齐
 *  • 默认悬浮在父级右下角（edgeMargin 边距），默认展开
 *  • 点击 Header（非拖动）触发展开/折叠；右侧展开按钮独立可点
 *  • 点击学科按钮发出 subjectClicked(subjectId)
 *
 *  注意：
 *  • 不使用 anchors —— drag.target 需要直接写 root.x/y，anchors 会与之冲突
 *  • Expander 的默认属性是 contentData（未命名子项会进内容区），
 *    拖动层/动画/Connections 必须显式挂到 data: [...] 才是 root 的直接子项
 */
Expander {
    id: root

    // —— 对外配置 —— //
    property int snapDuration: 380
    /** 从底部吸附位向上可拖动的最大距离 (px)；0 = 不限制 */
    property real maxDragUp: 420
    /** 悬浮边距（初始定位 + 底部吸附时与父级边缘的距离） */
    property real edgeMargin: 24
    property QtObject sourceItem


    signal subjectClicked(var subjectId)

    // —— 内部状态 —— //
    /** true = y 跟随「父级底部 - 当前高度」逐帧对齐（吸附态）；拖动/吸附动画期间为 false */
    property bool _followBottom: true
    /** 本次按下是否发生过真实拖动（区分点击与拖动） */
    property bool _wasDragged: false
    /** 用户是否主动拖动过 X（未拖过则父级宽度变化时重新贴右） */
    property bool _userMovedX: false
    /** 右侧展开按钮预留宽度（RinUI Expander 中 expandBtn.width + 边距 ≈ 45px），不覆盖此区域 */
    readonly property real _expandBtnReserved: 45

    expanded: true   // 默认展开

    /** 底部对齐（吸附态专用；吸附动画进行中不打断） */
    function _alignBottom() {
        if (_followBottom && parent && !_ySnapAnim.running)
            y = parent.height - height - edgeMargin
    }

    // 初始位置：父级右下角（不用 anchors，x/y 留给 drag.target 自由写）
    Component.onCompleted: {
        if (parent) {
            x = Math.max(0, parent.width - width - edgeMargin)
            y = parent.height - height - edgeMargin
        }
    }

    // 展开/收起动画中 implicitHeight 逐帧变化 → y 逐帧跟随，底部始终对齐
    onHeightChanged: _alignBottom()

    header: RowLayout {
        Layout.margins: 13
        Layout.leftMargin: 0
        spacing: 16

        RowLayout {
            Layout.maximumWidth: parent.width * 0.6
            spacing: 16

            Icon {
                size: 22
                name: "ic_fluent_line_horizontal_3_20_regular"
            }
            Text {
                Layout.fillWidth: true
                typography: Typography.Body
                text: qsTr("Quick Add Subject")
            }
        }
    }

    ColumnLayout {
        width: parent.width
        height: 200
        spacing: 8

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: _subjectsFlow.height
            clip: true

            ScrollBar.vertical: ScrollBar {}

            Flow {
                id: _subjectsFlow
                width: parent.width
                Repeater {
                    model: AppCentral.scheduleRuntime.subjects
                    Button {
                        enabled: !AppCentral.scheduleManager.isReadonly()
                        flat: true
                        icon.name: modelData.icon
                        text: modelData.name
                        onClicked: root.subjectClicked(modelData.id)
                    }
                }
            }
        }
    }

    // ================================================================
    // 拖动层 / 吸附动画 / 父级尺寸监听
    // 必须挂到 root.data（root 默认属性是 contentData，未命名子项会被塞进内容区）。
    // ================================================================
    data: [
        AcrylicBrush {
            blur: 32
            tintOpacity: 0.8
            sourceItem: root.sourceItem
        },
        MouseArea {
            id: _dragArea
            x: 0
            y: 0
            // 覆盖 header 区域，宽度避让右侧展开按钮预留区
            width: root.width - root._expandBtnReserved
            height: root.headerHeight
            z: 1000   // 高于 header Clip 与内部拦截层
            cursorShape: Qt.SizeAllCursor

            // Qt 标准拖动：全局光标追踪直接写 root.x/y，无手动坐标换算
            drag.target: root
            drag.axis: Drag.XAndYAxis
            drag.minimumX: 0
            drag.maximumX: root.parent ? root.parent.width - root.width : 0
            // Y：范围 = [底部吸附位 - maxDragUp, 底部吸附位]（吸附位按当前高度动态计算）
            drag.minimumY: root.parent
                ? Math.max(0, root.parent.height - root.height - root.edgeMargin - root.maxDragUp)
                : 0
            drag.maximumY: root.parent
                ? Math.max(0, root.parent.height - root.height - root.edgeMargin)
                : 0

            // 按下时 root 的位置（drag 组没有信号，靠 released 时比较位移判断是否拖动过）
            property real _pressX: 0
            property real _pressY: 0

            onPressed: (mouse) => {
                root._followBottom = false
                root._wasDragged = false
                if (_ySnapAnim.running) _ySnapAnim.stop()
                _pressX = root.x
                _pressY = root.y
            }

            onClicked: (mouse) => {
                // 非拖动点击 → 切换展开/折叠
                if (!root._wasDragged) root.expanded = !root.expanded
            }

            onReleased: (mouse) => {
                // released 先于 clicked 触发：位移超阈值 = 发生过拖动
                if (Math.abs(root.x - _pressX) > 3 || Math.abs(root.y - _pressY) > 3) {
                    root._wasDragged = true
                    if (Math.abs(root.x - _pressX) > 3) root._userMovedX = true
                }
                if (root._wasDragged) {
                    // 拖动松手：Y 弹性吸附回底部（高度此刻稳定，目标值按当前展开状态计算）
                    _ySnapAnim.to = root.parent
                        ? root.parent.height - root.height - root.edgeMargin
                        : root.y
                    _ySnapAnim.start()
                } else {
                    // 纯点击（可能已触发展开/收起动画）：恢复逐帧跟随，y 随高度动画平滑对齐
                    root._followBottom = true
                    root._alignBottom()
                }
            }
        },

        NumberAnimation {
            id: _ySnapAnim
            target: root
            property: "y"
            duration: root.snapDuration
            easing.type: Easing.OutBack
            easing.overshoot: 1.5
            onFinished: root._followBottom = true
        },

        // 父级（页面）尺寸变化时重新对齐
        Connections {
            target: root.parent
            function onHeightChanged() {
                root._alignBottom()
            }
            function onWidthChanged() {
                // 用户没拖过 X → 保持贴右；拖过 → 保持用户位置
                if (!root._userMovedX && root.parent)
                    root.x = Math.max(0, root.parent.width - root.width - root.edgeMargin)
            }
        }
    ]
}
