import QtQuick
import RinUI

// 课表速览条（扩展 classwidgets.ext.schedulePeek，见 extensions-feature-plan.md §7 阶段 D）。
// 挂在 WidgetsContainer 根 Column 中、widgetsFlow 之后：单行显示当天课程"缩写格"，
// 相邻两节课间隔 ≥ 阈值时插分组竖线，进行中课程橙色圆底；绿色"下一节"圆底仅在
// 课间/活动（非上课）时段点亮，上课期间下一节保持普通样式。
// 条宽与上方小组件行（widgetsFlow）对齐：alignWidth 由 WidgetsContainer 注入，
// 内容本身更宽（多课节 + 少小组件）时以内容宽兜底，避免格子溢出圆角底。
//
// 数据来源与字段（全部现成，零新增 C++ 接口）：
// - AppCentral.scheduleRuntime.currentDayEntries：当天全部条目（含 break/activity、
//   已应用 override）。元素是 normalizeEntry 后的 QVariantMap，字段
//   {id, type, startTime, endTime, subjectId, title}，startTime/endTime 为 "HH:MM"
//   字符串（ScheduleModel.h:14-20 模型注释 + ScheduleRuntime.cpp:431-438 的
//   toVariantMap 转换；条目里并没有 simplifiedName 字段——缩写挂在 subject 上）。
// - 课程名/缩写经 scheduleRuntime.subjects 按 subjectId 查
//   （范式照 widgets/upcomingActivities.qml:59-85 的 subjectById/首字先例）。
// - 状态观察：ScheduleRuntime::currentsChanged 是 C++ 专属聚合信号、不带 NOTIFY
//   属性语义（ScheduleRuntime.h:83），QML 侧一律走逐属性 NOTIFY
//   （currentEntryChanged/currentStatusChanged/currentDayEntriesChanged/
//   subjectsChanged/currentTimeChanged/timeOffsetChanged，ScheduleRuntime.h:86-101），
//   见下方 Connections 的 statusTick 触发器。
//
// 根对象是普通 Item：visible/width/height 的任何变化都能被外层观测——
// WidgetsContainer（D3，转发 contentGeometryChanged 驱动 MainInterface→C++ 蒙版
// 重算链）与 WidgetsWindow::updateMask（D4，按 objectName findChild 取几何并入并集）。
// hide/anchor/偏移无需特殊处理：本条在 Column 内自然参与排布，Column 尺寸变化
// 沿 MainInterface.qml:185-186 的 onWidth/HeightChanged 自动触发重算。
Item {
    id: schedulePeekBar
    objectName: "schedulePeekBar"

    // ── 外观常量（单行紧凑条：约 36px 高，宽度自适应内容） ──────────────
    readonly property real cellSize: 26   // 单格直径（圆形高亮底同径）
    readonly property real rowSpacing: 6  // 格间距（均匀）

    // 橙/绿按深浅主题微调，保证白字在其上可读（配色取法照 Widget.qml 的
    // Theme.isDark() 三元式先例）
    readonly property color currentColor: "#E8833A"
    readonly property color nextColor: Theme.isDark() ? "#37A05B" : "#3BA55D"

    // ── 配置读取（三键均已在 ConfigStore defaultConfig/kScalarSpecs 落地） ──
    // 逐层防御取键：坏配置（无 extensions 分区）时绑定抛异常会让整条速览消失，
    // 兜底默认值（auto/15）与 ConfigStore 默认一致
    readonly property var peekCfg: {
        const ext = Configs.data.extensions
        return (ext && ext.schedule_peek) ? ext.schedule_peek : {}
    }
    readonly property string peekMode: {
        // 白名单 auto|always（ConfigStore.cpp:61 kSchedulePeekMode），非法值按 auto
        return peekCfg.mode === "always" ? "always" : "auto"
    }
    readonly property int splitGapMinutes: Math.max(1, peekCfg.split_gap_minutes || 15)

    // isEnabled() 是 Q_INVOKABLE 函数，QML 绑定不会对函数调用建立通知依赖；
    // 显式在绑定内读取 Extensions.extensions 属性（NOTIFY extensionsChanged，
    // ExtensionManager.h:21），扩展开关切换后本绑定必然重算——比逐个监听
    // extensionToggled 更可靠（列表变化是开关生效的唯一充分信号）。
    readonly property bool extEnabled: {
        Extensions.extensions
        return Extensions.isEnabled("classwidgets.ext.schedulePeek")
    }

    // D2 要求的显式状态观察。绑定本身已随逐属性 NOTIFY 重算，这里再用
    // statusTick 计数兜底并集中触发（currentsChanged 不可用，见文件头注释），
    // peekData 绑定读取 statusTick 保证任一信号到来都重算一次。
    property int statusTick: 0
    Connections {
        target: AppCentral.scheduleRuntime
        function onCurrentEntryChanged() { schedulePeekBar.statusTick++ }
        function onCurrentStatusChanged() { schedulePeekBar.statusTick++ }
        function onCurrentDayEntriesChanged() { schedulePeekBar.statusTick++ }
        // 科目表与当天条目同源于课表推送，共用同一触发器
        function onSubjectsChanged() { schedulePeekBar.statusTick++ }
        function onTimeOffsetChanged() { schedulePeekBar.statusTick++ }
        // currentTimeChanged 每秒一发，但 next/ended 判定只到分钟粒度：仅在
        // "分钟变化"的那一拍更新 currentMinute，peekData 不直接读 currentTime
        // （否则绑定每秒重订阅、Repeater 整表每秒换模型白白 churn）
        function onCurrentTimeChanged() {
            const m = schedulePeekBar.hmToMinutes(AppCentral.scheduleRuntime.currentTime)
            if (m !== schedulePeekBar.currentMinute)
                schedulePeekBar.currentMinute = m
        }
    }
    // 当前时钟分钟数（不含 time_offset；peekData 里叠加）。-1 = 尚未初始化
    property int currentMinute: -1

    Component.onCompleted: currentMinute = hmToMinutes(AppCentral.scheduleRuntime.currentTime)

    // ── 工具函数 ──────────────────────────────────────────────
    // "HH:MM"（容错 "HH:MM:SS"）→ 当天分钟数；非法返回 -1
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

    // 取首字：Array.from 按码点切，避免劈开代理对（upcomingActivities.qml:82 同款）
    function firstChar(text) {
        const chars = Array.from(String(text))
        return chars.length ? chars[0] : ""
    }

    function subjectById(subjects, id) {
        if (!id)
            return null
        for (let i = 0; i < subjects.length; i++) {
            if (subjects[i].id === id)
                return subjects[i]
        }
        return null
    }

    // 缩写规则（阶段 D 统一取"首字"，与速览格等宽；区别于 upcomingActivities
    // 的"多字缩写原样显示"）：
    // 1) 覆盖条目的自定义标题优先（subjectId 被替换时 title 已清空，
    //    title 存在即用户显式命名，见 ScheduleRuntime.cpp:263-265）；
    // 2) 科目设了 simplifiedName → 取其首字（多字缩写同样只取首字）；
    // 3) 未设缩写 → 经 subjects 按 subjectId 查课程全名取首字；
    // 4) 查不到科目也查不到名（数据异常）→ "#" 占位，保留格子使时间轴不错位。
    function abbrevOf(entry, subjects) {
        if (entry.title)
            return firstChar(entry.title)
        const subject = subjectById(subjects, entry.subjectId)
        if (subject && subject.simplifiedName)
            return firstChar(subject.simplifiedName)
        if (subject && subject.name)
            return firstChar(subject.name)
        return "#"
    }

    // ── 核心映射：当天课节 → 格子序列 + 显隐判据 ─────────────────────
    readonly property var peekData: {
        schedulePeekBar.statusTick  // 显式重算触发器（见上方 Connections）
        const rt = AppCentral.scheduleRuntime
        const dayEntries = rt.currentDayEntries || []
        const subjects = rt.subjects || []

        // 过滤 type=="class" 并按开始时间排序（C++ getDisplayEntries 已稳定排序，
        // 这里防御乱序数据不插错竖线；时间不可解析的脏条目直接丢弃）
        const classes = []
        for (let i = 0; i < dayEntries.length; i++) {
            const e = dayEntries[i]
            if (e.type !== "class")
                continue
            const start = hmToMinutes(e.startTime)
            const end = hmToMinutes(e.endTime)
            if (start < 0)
                continue
            classes.push({ id: String(e.id || ""), start: start, end: end < 0 ? start : end,
                           entry: e })
        }
        classes.sort(function (a, b) { return a.start - b.start })

        // "现在"= 真实时钟分钟数（currentMinute，由 currentTimeChanged 逐分更新）
        // + schedule.time_offset（秒，ScheduleRuntime.cpp:409 addSecs(newOffset)），
        // 与 C++ 状态机同一基准；按分钟粒度即可（下课/上课边界都落在整分）
        const nowMinutes = schedulePeekBar.currentMinute
            + Math.round((rt.timeOffset || 0) / 60)

        // 进行中：currentEntry 恰为 class 条目（id 为 string，见 normalizeEntry）
        const cur = rt.currentEntry
        const currentId = (cur && cur.type === "class") ? String(cur.id || "") : ""
        // 是否处于上课中：决定"下一节"绿底是否点亮（课间/活动时才高亮）
        const inClassNow = currentId !== "" || rt.currentStatus === "class"

        let endedCount = 0
        let futureCount = 0
        let nextId = ""
        for (let i = 0; i < classes.length; i++) {
            if (classes[i].end <= nowMinutes)
                ++endedCount
            if (classes[i].start > nowMinutes) {
                ++futureCount
                if (!nextId)
                    nextId = classes[i].id
            }
        }

        // 竖线插在"相邻两节 class"之间：间隔 = 后节 start − 前节 end 的实际分钟数
        // （其间 break/activity 不单独成格，时间差天然包含它们）≥ 阈值才插；
        // 普通 10 分钟课间不插（产品决策 §11-Q2）。sep 格必须显式 highlight:"none"：
        // delegate 的白字判定按 highlight !== "none" 走，字段缺省（undefined）会让
        // 竖线误入白字分支，绿圆底判定也只认显式值。
        const gap = schedulePeekBar.splitGapMinutes
        const cells = []
        for (let i = 0; i < classes.length; i++) {
            const c = classes[i]
            if (i > 0 && c.start - classes[i - 1].end >= gap)
                cells.push({ kind: "sep", highlight: "none" })
            // 绿色圆底只标"课间/活动时段的下一节"；上课期间下一节正常显示
            cells.push({
                kind: "cell",
                text: schedulePeekBar.abbrevOf(c.entry, subjects),
                highlight: c.id && c.id === currentId ? "current"
                    : (!inClassNow && c.id && c.id === nextId ? "next" : "none"),
            })
        }

        return {
            cells: cells,
            classCount: classes.length,
            endedCount: endedCount,
            futureCount: futureCount,
            inClass: inClassNow,
        }
    }

    // ── 显隐状态机（D2） ─────────────────────────────────────
    // 两模式在当天无 class 条目（周末/空课表）时均隐藏；扩展开关关闭整条隐藏。
    // auto（产品决策 §11-Q4）：下课瞬间弹出、进入上课条目即收起、课前/放学后隐藏。
    //   "课间全程可见"要求有下一节可上：futureCount ≥ 1 —— 若不加这条，最后一节
    //   结束后 currentEntry 为空（free 态），按"至少已结束一节且不在上课"仍会
    //   命中显示，与"放学后隐藏"矛盾；futureCount≥1 即"还没到放学"。
    // always：当天有课即常驻（含上课期间）。
    readonly property bool shouldShow: {
        if (!extEnabled || peekData.classCount === 0)
            return false
        if (peekMode === "always")
            return true
        return peekData.endedCount >= 1 && !peekData.inClass && peekData.futureCount >= 1
    }

    visible: shouldShow
    // 收起瞬间 visible=false，不做退场动画（下课→上课要求"即收起"，
    // 退场动画反而拖慢状态切换）；弹出动画在下方 slideWrapper 上：
    // 滑出（自上方 −8px，配 clip 逐帧露出）+ 淡入
    clip: true

    // 宽度与上方小组件行对齐（alignWidth 由 WidgetsContainer 绑到 widgetsFlow.width）；
    // 取 max 是防内容溢出：课节数多而小组件少时圆角底至少包住全部格子
    property real alignWidth: 0
    width: Math.max(barBackground.implicitWidth, alignWidth)
    height: barBackground.implicitHeight

    // 视觉层：只动它自己的 y/opacity，不影响 Column 布局几何（蒙版取的是
    // 根 Item 静态矩形，逐帧几何变化由根 visible/width/height 信号驱动 D3 链）
    Item {
        id: slideWrapper
        anchors.fill: parent
        y: schedulePeekBar.shouldShow ? 0 : -8
        opacity: schedulePeekBar.shouldShow ? 1 : 0
        Behavior on y {
            NumberAnimation { duration: 260; easing.type: Easing.OutQuint }
        }
        Behavior on opacity {
            NumberAnimation { duration: 240; easing.type: Easing.OutCubic }
        }

      // 半透明圆角底：保证悬浮在桌面上时文字可读（底色取法照
      // ClassWidgets/Theme/components/Widget.qml:11-14 深浅主题先例）
      Rectangle {
          id: barBackground
          anchors.fill: parent
          implicitWidth: peekRow.implicitWidth + 16   // 左右各 8 内边距
          implicitHeight: peekRow.implicitHeight + 10 // 上下各 5，整条约 36px
          radius: 10
          color: Theme.isDark() ? Qt.alpha("#1E1D22", 0.65) : Qt.alpha("#FBFAFF", 0.7)
          border.width: 1
          border.color: Qt.alpha(Theme.currentTheme.colors.textColor, 0.12)
      }

      Row {
          id: peekRow
          anchors.centerIn: parent
          spacing: schedulePeekBar.rowSpacing

          Repeater {
              model: schedulePeekBar.peekData.cells

              delegate: Item {
                  id: cell
                  required property int index
                  required property var modelData

                  // 竖线格：1px 细线；普通格：cellSize 方形
                  width: modelData.kind === "sep" ? 1 : schedulePeekBar.cellSize
                  height: schedulePeekBar.cellSize

                  // 高亮圆形底叠于单字之下（只认两种显式高亮值，字段缺失不画）
                  Rectangle {
                      visible: cell.modelData.highlight === "current"
                          || cell.modelData.highlight === "next"
                      width: schedulePeekBar.cellSize - 4
                      height: width
                      radius: width / 2
                      anchors.centerIn: parent
                      color: cell.modelData.highlight === "current"
                          ? schedulePeekBar.currentColor
                          : schedulePeekBar.nextColor
                  }

                  Text {
                      anchors.centerIn: parent
                      visible: cell.modelData.kind !== "sep"
                      text: cell.modelData.text
                      font.family: AppCentral.getQFont(
                          Configs.data.preferences.font, Utils.fontFamily).family
                      font.pixelSize: 14
                      font.weight: Configs.data.preferences.font_weight || 600
                      // 高亮格用白字压在圆底上；普通格与竖线跟随主题文字色
                      color: cell.modelData.highlight !== "none"
                          ? "white" : Theme.currentTheme.colors.textColor
                  }

                  // 竖线本体（半透明，弱化为分组提示而非内容）
                  Rectangle {
                      visible: cell.modelData.kind === "sep"
                      anchors.verticalCenter: parent.verticalCenter
                      width: 1
                      height: schedulePeekBar.cellSize * 0.55
                      color: Qt.alpha(Theme.currentTheme.colors.textColor, 0.45)
                  }
              }
          }
      }
        } // slideWrapper 视觉层
}
