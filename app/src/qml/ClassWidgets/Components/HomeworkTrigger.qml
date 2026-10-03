import QtQuick

// 当日作业自动显隐触发器（扩展 classwidgets.ext.homework，F1/F3）。
// 常驻挂载在 MainInterface（主窗口生命周期 = 应用生命周期，作业浮窗本身关闭即
// 销毁，不能在浮窗内做触发）。状态源只读 AppCentral.scheduleRuntime，零自开
// 秒级 QTimer（延迟/600ms 通知定时器均为单发业务定时器，非心跳）。
//
// 触发规则（定稿 F1）：
// - 下课：currentStatus 从 class 切到 break/free（不含 preparation/activity——
//   activity 段（自习/活动）保持待发定时器继续跑，到期 fire 时按当时状态复核）；
//   放学后最后一段 free 同样触发；
// - 延迟 delay_minutes ∈ [0,10]：单发 Timer restart，延迟时长在触发沿快照；
//   期间切回 class/preparation → stop 并隐藏浮窗（连续课间 < delay 则本次不弹）；
//   到期时复核当前状态仍为 break/free，状态已漂移（调休/切课表/休眠唤醒）则取消；
// - 显隐：timer 到期 → WindowManager.openHomeworkFloat()；auto_show=false 永不自动弹；
//   上课/预备（status==class||preparation）→ 一律自动隐藏，不区分自动弹出与
//   手动打开——否则启动补开/扩展开关开启/设置页手动打开的浮窗将因标志位
//   永久豁免隐藏，表现为“常驻显示”；
// - 通知：delay==0 时在下课铃后约 600ms 发（新通知自动覆盖旧通知，无队列堆积）；
//   上节课科目 needsHomework==false 或 notify_enabled=false 则不发；浮窗不受影响；
//   去重指纹「日期|上节课id|endTime」只作用于通知（F3 定稿），浮窗触发不套指纹——
//   否则「拖堂后再次下课」（同 id 同 endTime）会把浮窗也吞掉。
Item {
    id: trigger

    // Configs/AppCentral 均判空：应用退出拆引擎时上下文属性先于 QML 对象销毁，
    // 绑定会再求值一次，不判空会刷 TypeError（与 SchedulePeekBar 同类噪声）
    readonly property var hwCfg: {
        const ext = (Configs && Configs.data) ? Configs.data.extensions : null
        return (ext && ext.homework) ? ext.homework : {}
    }
    // isEnabled() 是 Q_INVOKABLE，绑定内显式读 Extensions.extensions 建立依赖。
    // Extensions 判空理由同上（退出拆引擎时绑定会再求值一次）
    readonly property bool extEnabled: {
        if (!Extensions)
            return false
        Extensions.extensions
        return Extensions.isEnabled("classwidgets.ext.homework")
    }
    readonly property int delayMinutes: Math.max(0, Math.min(10, hwCfg.delay_minutes || 0))
    readonly property bool autoShow: hwCfg.auto_show !== false
    readonly property bool notifyEnabled: hwCfg.notify_enabled !== false

    // 状态迁移跟踪与本次下课的待发内容
    property string lastStatus: ""
    property string pendingKey: ""            // 本次下课的指纹（仅通知去重消费）
    property string lastNotifyKey: ""         // 最近一次实际播报的指纹
    property string pendingSubjectName: ""
    property bool pendingNeedsHomework: true
    property int pendingDelayMs: 0            // 触发沿快照，fire 时不再读实时配置

    Component.onCompleted: trigger.lastStatus = trigger.runtimeStatus()

    function runtimeStatus() {
        const runtime = AppCentral ? AppCentral.scheduleRuntime : null
        return runtime ? runtime.currentStatus : ""
    }

    Connections {
        target: AppCentral ? AppCentral.scheduleRuntime : null
        function onCurrentStatusChanged() { trigger.handleStatus(trigger.runtimeStatus()) }
    }

    // 拖堂延迟（单发业务定时器）；delay==0 时 fire 立即
    Timer {
        id: delayTimer
        repeat: false
        onTriggered: trigger.fire()
    }
    // delay==0 时在下课铃通知后 600ms 再发（QML 通知覆盖语义）
    Timer {
        id: notifyTimer
        interval: 600
        repeat: false
        onTriggered: trigger.sendNotification()
    }

    function handleStatus(status) {
        const previous = trigger.lastStatus
        trigger.lastStatus = status
        if (!trigger.extEnabled) {
            // 扩展关闭：C++ 侧（extensionToggled）已负责关窗，这里复位本次
            // 会话的待发状态，避免重开扩展后被脏标志/迟到定时器误触发
            delayTimer.stop()
            notifyTimer.stop()
            trigger.pendingKey = ""
            return
        }

        if (status === "class" || status === "preparation") {
            // 运行期间切回上课/预备：一律收起浮窗（F1 定稿，不区分自动/手动打开；
            // 否则启动补开/开关开启/设置页手动打开的窗口永不自动隐藏即“常驻”）
            trigger.cancelPending()
            return
        }
        // activity（自习/活动段）：按定稿不算下课（currentStatus 必须是
        // break/free 才触发），也不取消。两点边界，均属定稿明确行为：
        // - class→activity→break 的日程：本次没有下课沿，浮窗/通知缺失（不是迟到）；
        // - break→activity：已排期的延迟定时器继续跑，到期 fire() 因状态复核
        //   （status 必须回到 break/free）被取消
        if (status !== "break" && status !== "free")
            return
        if (previous !== "class")
            return // 只认「刚从 class 切出」；break↔free 之间不再重复触发

        const entry = trigger.lastClassEntry()
        if (!entry)
            return
        trigger.pendingKey = trigger.dateKey() + "|" + String(entry.id || "")
            + "|" + String(entry.endTime || "")

        trigger.pendingSubjectName = trigger.subjectName(entry.subjectId)
        // needsHomework==false 的课：浮窗照常显示，仅抑制通知（定稿）
        trigger.pendingNeedsHomework = trigger.needsHomework(entry.subjectId)
        trigger.pendingDelayMs = trigger.delayMinutes * 60 * 1000

        if (trigger.pendingDelayMs > 0)
            delayTimer.restart(trigger.pendingDelayMs)
        else
            trigger.fire()
    }

    function fire() {
        if (!trigger.extEnabled)
            return
        // 延迟期间状态漂移（调休/切课表/休眠唤醒）：按取消处理，不弹不播
        const status = trigger.runtimeStatus()
        if (status !== "break" && status !== "free") {
            trigger.cancelPending()
            return
        }
        if (!WindowManager)
            return
        if (trigger.autoShow) {
            WindowManager.openHomeworkFloat()
        }
        if (trigger.notifyEnabled && trigger.pendingNeedsHomework) {
            if (trigger.pendingDelayMs > 0)
                trigger.sendNotification()
            else
                notifyTimer.restart()
        }
    }

    function cancelPending() {
        delayTimer.stop()
        notifyTimer.stop()
        // 上课/预备一律收起浮窗（F1 定稿）：不区分自动弹出与手动打开。
        // 旧逻辑仅收起“本触发器打开的”窗口，导致启动补开/扩展开关开启/
        // 设置页手动打开的窗口在后续上课时永不关闭，即“常驻显示”。
        // closeHomeworkFloat 幂等（无窗时 no-op），可无条件调用。
        if (WindowManager) {
            WindowManager.closeHomeworkFloat()
        }
    }

    function sendNotification() {
        notifyTimer.stop()
        if (!trigger.extEnabled || !trigger.notifyEnabled || !trigger.pendingNeedsHomework)
            return
        // 去重指纹只管通知（F3）：同一天同节课（同 id 同 endTime）只播报一次，
        // 浮窗触发不受此限制
        if (!trigger.pendingKey || trigger.pendingKey === trigger.lastNotifyKey)
            return
        if (!Homework)
            return
        trigger.lastNotifyKey = trigger.pendingKey
        Homework.notify(trigger.pendingSubjectName)
    }

    // ── 数据现算（SchedulePeekBar 同款范式） ──
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

    function currentMinutes() {
        const runtime = AppCentral ? AppCentral.scheduleRuntime : null
        if (!runtime)
            return -1
        const minute = trigger.hmToMinutes(runtime.currentTime)
        if (minute < 0)
            return -1
        return minute + Math.round((runtime.timeOffset || 0) / 60)
    }

    function dateKey() {
        const runtime = AppCentral ? AppCentral.scheduleRuntime : null
        const date = runtime ? runtime.currentDate : null
        if (!date)
            return ""
        return date.year + "-" + String(date.month).padStart(2, "0")
            + "-" + String(date.day).padStart(2, "0")
    }

    // 上节课：当天 class 条目中 end<=now 且 end 最大者（放学后最后一段 free 也命中）
    function lastClassEntry() {
        const runtime = AppCentral ? AppCentral.scheduleRuntime : null
        const entries = runtime ? (runtime.currentDayEntries || []) : []
        const nowMinutes = trigger.currentMinutes()
        let best = null
        let bestEnd = -1
        for (let i = 0; i < entries.length; ++i) {
            const entry = entries[i]
            if (entry.type !== "class")
                continue
            const end = trigger.hmToMinutes(entry.endTime)
            if (end < 0 || end > nowMinutes)
                continue
            if (end > bestEnd) {
                bestEnd = end
                best = entry
            }
        }
        return best
    }

    function subjectName(subjectId) {
        if (!subjectId)
            return ""
        const runtime = AppCentral ? AppCentral.scheduleRuntime : null
        const subjects = runtime ? (runtime.subjects || []) : []
        for (let i = 0; i < subjects.length; ++i) {
            if (subjects[i].id === subjectId)
                return subjects[i].name || ""
        }
        return ""
    }

    function needsHomework(subjectId) {
        if (!subjectId)
            return true // 查不到科目按需要布置处理（通用文案）
        const runtime = AppCentral ? AppCentral.scheduleRuntime : null
        const subjects = runtime ? (runtime.subjects || []) : []
        for (let i = 0; i < subjects.length; ++i) {
            if (subjects[i].id === subjectId)
                return subjects[i].needsHomework !== false
        }
        return true
    }
}
