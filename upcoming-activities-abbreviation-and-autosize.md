# 即将上课小组件：显示缩写开关 + 组件宽度自适应（实施计划）

- 状态：**已执行（2026-09-26）**，登记于 `QML_MODIFICATIONS.md` 改动 10。与代码草案的两处偏差（均为运行时实测修正）：
  ① `TextMetrics` 在 Qt 6.10.3 中属性名为 `advanceWidth`（无 `advance`）；② delegate 不能同时 `width: implicitWidth` + `elide`（Binding loop，内容区会空白）——省略号改由 refit() 用 `elide/elideWidth/elidedText` 预生成，delegate 宽度由 per-delegate `TextMetrics.advanceWidth` 驱动。zh_HK 译文按文件惯例使用繁体。
- 定稿日期：2026-09-26
- 目标仓库：`D:\WorkBuddy_WorkSpace\Class Widgets Next\Class_Widgets_Next`（C++/Qt6 移植版，Qt 6.10.3 / MSVC / Ninja）
- 参考上游：`D:\WorkBuddy_WorkSpace\Class Widgets Next\Class-Widgets-2-main`（Python 版，**不修改**）

---

## 1. 已确认的需求决策

| # | 议题 | 结论 |
|---|---|---|
| 1 | 适用范围 | 只针对标准形态（小组件条上的「即将上课」）。迷你模式/浮窗共用同一 QML，行为顺带统一，不做特殊分支 |
| 2 | 节数上限 | 两种模式都**最多 7 节**；由「最多活动数」数字框手动输入（1–7） |
| 3 | 滚动 | **彻底移除** Marquee，永不滚动；放不下的课从末尾裁掉，保证已显示的课程名完整 |
| 4 | 默认值 | **默认显示缩写**；缩写 = 该课在科目设置里的「缩写」，未设置则取全称第一个字（地理→地，历史→历） |
| 5 | 组件宽度 | 随显示内容自适应变宽/变窄 |
| 6 | 配置键 | 沿用存量键 `full_name` / `max_activities`（**不改名**），UI 上以「显示缩写」呈现并取反绑定 |

---

## 2. 现状（已核实的代码事实）

### 2.1 涉及文件

| 文件 | 角色 |
|---|---|
| `app/src/qml/widgets/upcomingActivities.qml` | 小组件本体（上游同步区文件） |
| `app/src/qml/widgets/settings/upcomingActivities.qml` | 小组件编辑界面（右键→编辑）（上游同步区文件） |
| `src/core/BuiltinWidgets.cpp` | 内置组件注册 + 默认设置（第 61–64 行 `upcomingDefaults`） |
| `src/core/WidgetsModel.cpp` | 组件实例与 settings 持久化（`loadPreset` 先合默认再合存量；`updateSettings` 合并写回） |
| `app/src/qml/ClassWidgets/Components/dialogs/WidgetSettingsDialog.qml` | 设置弹窗：载入 settings 页，`onAccepted` 时 `WidgetsModel.updateSettings` |
| `app/src/qml/ClassWidgets/Components/WidgetsContainer.qml` / `WidgetLoader.qml` | 小组件条渲染；宽度取 `loader.width`（跟随 item 的 implicitWidth），宽度变化会触发窗口 mask 更新 |
| `app/src/qml/ClassWidgets/Theme/components/BaseWidget.qml` | `implicitWidth: Math.max(headerRow.implicitWidth, contentArea.childrenRect.width) + 48`；`height: miniMode ? 56 : 100`；`padding: miniMode ? 16 : 24` |
| `app/src/qml/ClassWidgets/Theme/components/Title.qml` | 字体：`AppCentral.getQFont(Configs.data.preferences.font, Utils.fontFamily)`，weight = `font_weight \|\| 600`，`px: miniMode ? 20 : 28`（400ms 动画） |
| `app/src/qml/ClassWidgets/Theme/components/MarqueeTitle.qml` | 滚动组件，`implicitWidth: Math.min(label.implicitWidth, maximumWidth)`（本次将不再使用） |
| `app/assets/locales/*.ts` + `*.qm` | 翻译；QML 的 `qsTr` 上下文 = **文件基名**，故 widget 与其 settings 页共用上下文 `upcomingActivities` |

### 2.2 当前行为（为何要改）

1. 默认 `marquee: true` → 用 `MarqueeTitle`（写死 `width: 275`）滚动显示 5 节课；
2. `full_name: false` 才走缩写分支，且回退是 `simplifiedName || name.substring(0, 3)`（**取前 3 字**），所以只有给每节课手动设了「缩写」才勉强好用；
3. 现存小 bug：拼接分隔符时判断的是 `i === entries.length - 1`，当 `max_activities` 截断时，最后一节课后面会多出一对空格；
4. 组件本身其实已经能自适应宽度（`BaseWidget.implicitWidth` 跟随内容），只是被写死的 275px 滚动窗口挡住。

---

## 3. 目标行为规格

### 3.1 显示条数

```
显示条数 = min(待办条目数, max_activities, 7)
max_activities = clamp(settings.max_activities, 1, 7)   // 缺省/非法 → 7
```
再叠加宽度裁剪（见 3.3），最终显示 `fittingCount` 条。

### 3.2 单条显示文本（含示例）

```
full = entry.title（活动/自定义名）
     ?? 科目全称 subject.name
     ?? 兜底标签（qsTr("Class") / qsTr("Activity") / qsTr("Unset")）

显示缩写关闭 → 显示 full
显示缩写开启 → 若 full 来自科目全称 且 该科目设置了 simplifiedName（非空）→ 原样显示该缩写（多字不截断，如「现代」）
             → 否则取 full 的第一个字（用 Array.from(full)[0]，避免劈开代理对）
```

| 场景 | 科目名 | 缩写设置 | 关闭缩写 | 开启缩写 |
|---|---|---|---|---|
| 常规课 | 地理 | 空 | 地理 | **地** |
| 常规课 | 历史 | 空 | 历史 | **历** |
| 常规课 | 信息技术 | 数 | 信息技术 | **数** |
| 常规课 | 信息技术 | 信息技术 | 信息技术 | **信息技术**（原样） |
| 活动（自带 title） | — | — | 班会 | **班**（取 title 首字，不用科目缩写） |
| 无科目无 title | — | — | 课程 / 活动 / 未设置 | 课 / 活 / 未 |

> 说明：若某节课同时有自定义 title 和带缩写的科目，本次按「title 优先取首字」处理（保证显示的是所见文本的缩写）；如需改成「科目缩写永远优先」，只需调整 `resolveEntryText` 一行。

### 3.3 宽度自适应算法

- 每节课一个 `Title` 文本，按固定间距 `16`（等价现在的两个空格）排进一个 `Row`；`Row.width = implicitWidth`，从而驱动 `BaseWidget.implicitWidth` → 组件宽度自适应（去掉 `MarqueeTitle` 的 275px 写死宽度）；
- **宽度上限**（防止超长课程名把组件顶出屏幕）：
  ```
  maxContentWidth = max(80, (Screen.width - 96) / scale_factor - 48)
                    （96 = 桌面左右安全边距；48 = BaseWidget 固定左右内边距；按缩放系数折算）
  ```
- **裁剪规则**：从第 1 条起逐条累加（宽度 + 间距），放不下就停 → 只保留能**完整**显示的课程（宁可少显示几节，也不截断课程名）；
- **保底**：若连第 1 节课都超宽，则该条限宽为 `maxContentWidth` 并 `Text.ElideRight` 省略号。

### 3.4 边界行为

| 情况 | 表现 |
|---|---|
| 没有待办条目 | 显示 `qsTr("Nothing ahead")`（沿用原逻辑，占 1 条文本） |
| 课程名很长 | 末尾课程被裁掉（不截断文字） |
| 迷你模式 | 同一套逻辑，字号 20px（`Title.px` 400ms 动画期间逐帧重算裁剪） |
| 缩放 ≠ 100% | 宽度上限按 `scale_factor` 折算 |
| 切换语言 | 兜底标签（Class/Activity/Unset）随语言刷新（沿用 `AppCentral.translator.language` 触发重算的既有技巧） |
| 主题切换 | 组件被 Loader 重建，重新走一遍 `refit()` |
| 编辑模式/添加组件预览 | 预览 Loader 用 `default_settings` 注入 settings，行为与线上一致 |

---

## 4. 逐文件改动清单

### 4.1 `app/src/qml/widgets/upcomingActivities.qml`（整体重写渲染部分）

要点：
1. 删掉 `MarqueeTitle`，改为「每节课一个 `Title`」排成 `Row`；
2. 新增 `showAbbreviation` / `displayTexts` / `resolveEntryText` / `refit()`；
3. 新增 `import QtQuick.Window`（`Screen`，与 `WidgetsContainer.qml` 同样做法）；
4. 尾随分隔符 bug 随 Row 方案自然消失。

代码草案（最终落地形态）：

```qml
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window          // 新增：Screen（宽度上限要用屏幕宽）
import RinUI
import ClassWidgets.Theme
import Qt5Compat.GraphicalEffects

Widget {
    id: root
    text: {
        AppCentral.translator.language
        return qsTr("Upcoming")
    }
    property bool isExternalClass: AppCentral.scheduleRuntime.currentSubject.isLocalClassroom === false
    property bool showLeaveHint: false

    property var entries: AppCentral.scheduleRuntime.nextEntries || []
    property var subjects: AppCentral.scheduleRuntime.subjects || []

    // ---- 显示规则（本次新增）----
    // 「显示缩写」开关：沿用存量键 full_name（键名不变，UI 上以"显示缩写"呈现，取反）
    readonly property bool showAbbreviation: (!settings || settings.full_name === undefined)
        ? true
        : !settings.full_name

    readonly property int maxActivities: Math.max(
        1, Math.min(7, (settings && settings.max_activities) || 7))

    readonly property real entrySpacing: 16

    // 宽度上限：BaseWidget 的 implicitWidth = 内容宽 + 48；再给桌面留 96 安全边距，按缩放折算
    readonly property real maxContentWidth: Math.max(
        80, (Screen.width - 96) / (Configs.data.preferences.scale_factor || 1.0) - 48)

    readonly property var displayTexts: {
        AppCentral.translator.language          // 语言切换时强制重算（下面用到了 qsTr）
        const result = []
        const count = Math.min(entries.length, maxActivities)
        for (let i = 0; i < count; i++)
            result.push(resolveEntryText(entries[i]))
        return result
    }

    // 由 refit() 计算：实际能完整放下的条数 / 首条是否需要省略号
    property int fittingCount: 0
    property bool elideFirst: false

    function subjectById(id) {
        for (let i = 0; i < subjects.length; i++) {
            if (subjects[i].id === id)
                return subjects[i]
        }
        return null
    }

    function fallbackText(type) {
        return type === "class" ? qsTr("Class")
             : type === "activity" ? qsTr("Activity")
             : qsTr("Unset")
    }

    function resolveEntryText(entry) {
        const subject = subjectById(entry.subjectId)
        const fromTitle = !!entry.title
        const full = entry.title || (subject && subject.name) || fallbackText(entry.type)
        if (!showAbbreviation)
            return full
        // 文本来自科目全称且该科目设了缩写 → 原样用（多字不截断）
        if (!fromTitle && subject && subject.simplifiedName)
            return subject.simplifiedName
        const chars = Array.from(full)          // 不用 charAt(0)，避免劈开代理对
        return chars.length ? chars[0] : full
    }

    // 逐条累加宽度，放不下就停 —— 保证已显示的课程名都是完整的
    function refit() {
        const total = displayTexts.length
        if (total === 0) {
            if (fittingCount !== 0) fittingCount = 0
            if (elideFirst) elideFirst = false
            return
        }
        const limit = maxContentWidth
        let used = 0
        let count = 0
        let firstWidth = 0
        for (let i = 0; i < total; i++) {
            measure.text = displayTexts[i]
            const w = measure.advance          // Qt6: advance（advanceWidth 已废弃）
            if (i === 0) firstWidth = w
            const need = (count === 0 ? 0 : entrySpacing) + w
            if (count > 0 && used + need > limit) break
            used += need
            count++
        }
        if (fittingCount !== count) fittingCount = count
        const needElide = firstWidth > limit
        if (elideFirst !== needElide) elideFirst = needElide
    }

    onDisplayTextsChanged: refitTimer.restart()
    onMaxContentWidthChanged: refitTimer.restart()
    Component.onCompleted: refitTimer.restart()

    Timer {
        id: refitTimer
        interval: 0            // 同一轮事件循环内合并（课表刷新 / 字号动画逐帧）
        onTriggered: root.refit()
    }

    Row {
        id: entryRow
        anchors.centerIn: parent
        spacing: root.entrySpacing
        width: implicitWidth   // 交给 BaseWidget 的 childrenRect 驱动组件宽度自适应

        Repeater {
            id: entryRepeater
            model: root.displayTexts.length

            delegate: Title {
                required property int index
                text: root.displayTexts[index] || ""
                visible: index < root.fittingCount
                elide: Text.ElideRight
                // 隐藏项 width 置 0：即便位置管理器仍参与布局，Row 的实际占位也会收缩
                width: index >= root.fittingCount ? 0
                     : (index === 0 && root.elideFirst ? root.maxContentWidth : implicitWidth)

                onImplicitWidthChanged: refitTimer.restart()   // 字体/字号变化后重算
                onTextChanged: refitTimer.restart()
            }
        }
    }

    // 量宽工具（非 Item，不参与 childrenRect）。字体跟随第一条文本，
    // 绑定里读的是子属性，字体变化会重新求值。
    TextMetrics {
        id: measure
        readonly property var sample: entryRepeater.count > 0 ? entryRepeater.itemAt(0) : null
        font.family: sample ? sample.font.family : ""
        font.pixelSize: sample ? sample.font.pixelSize : 28
        font.weight: sample ? sample.font.weight : Font.Normal
        font.italic: sample ? sample.font.italic : false
        font.letterSpacing: sample ? sample.font.letterSpacing : 0
    }
}
```

> 空列表时仍要显示「暂无课程」：在 `displayTexts` 为空时让 `title` 走 `qsTr("Nothing ahead")`（实现时二选一：把兜底文案作为 `displayTexts` 的唯一元素，或保留一个隐藏的 `Title` 承载 `Nothing ahead`；推荐前者，宽度同样自适应）。

### 4.2 `app/src/qml/widgets/settings/upcomingActivities.qml`

- 删除「滚动标题 / Marquee Title」整张 `SettingCard`；
- 「最多活动数」SpinBox 限定 `from: 1`、`to: 7`（RinUI `SpinBox` 默认 `editable: true`，可手动键入，越界自动收敛）；
- 「显示活动全称」开关改为「**显示缩写**」开关，状态与 `settings.full_name` **取反**绑定；
- 描述文案：沿用现有字符串（避免无谓的翻译 churn），新增一条缩写说明。

代码草案：

```qml
SettingsLayout {
    SettingCard {
        Layout.fillWidth: true

        icon.name: "ic_fluent_broad_activity_feed_20_regular"
        title: qsTr("Max number of activities")
        description: qsTr("Set the maximum number of activities to display in the upcoming activities view")

        SpinBox {
            id: maxActivitiesSpinBox
            from: 1
            to: 7
            onValueChanged: {
                settings.max_activities = maxActivitiesSpinBox.value
            }
            Component.onCompleted: {
                const saved = settings.max_activities
                maxActivitiesSpinBox.value = (saved >= 1 && saved <= 7) ? saved : 7
            }
        }
    }
    SettingCard {
        Layout.fillWidth: true

        icon.name: "ic_fluent_text_case_title_20_regular"
        title: qsTr("Show abbreviation")
        description: qsTr("Use the abbreviation set for each subject, or its first character when unset")

        Switch {
            id: showAbbreviationSwitch
            onCheckedChanged: {
                settings.full_name = !showAbbreviationSwitch.checked
            }
            Component.onCompleted: {
                showAbbreviationSwitch.checked = !(settings.full_name === true)
            }
        }
    }
}
```

### 4.3 `src/core/BuiltinWidgets.cpp`（第 61–64 行）

```diff
     QVariantMap upcomingDefaults;
-    upcomingDefaults.insert(QStringLiteral("marquee"), true);
-    upcomingDefaults.insert(QStringLiteral("max_activities"), 5);
-    upcomingDefaults.insert(QStringLiteral("full_name"), true);
+    // 即将上课默认显示缩写、默认最多 7 节；full_name=false 即"显示缩写"（键名沿用上游存量契约）
+    upcomingDefaults.insert(QStringLiteral("max_activities"), 7);
+    upcomingDefaults.insert(QStringLiteral("full_name"), false);
```
（`marquee` 键删除；老配置里残留的 `marquee` 会被忽略，不做迁移清理。）

### 4.4 `QML_MODIFICATIONS.md`（追加「改动 10」条目）

仓库纪律：`app/src/qml/**` 是上游同步区，任何改动必须登记。追加内容（示例）：

```markdown
### 9. 「即将上课」组件：显示缩写开关 + 宽度自适应（2026-09-26）

| # | 文件 | 改动 |
|---|---|---|
| 9.1 | `widgets/upcomingActivities.qml` | 移除 `MarqueeTitle`（滚动），改为每节课一个 `Title` 排进 `Row`，宽度随内容自适应；新增缩写解析（`simplifiedName` → 全称首字）、最多 7 节、超出屏幕宽度时从末尾裁剪（保底首条 `ElideRight`）。新增 `import QtQuick.Window`（取 `Screen.width` 算宽度上限） |
| 9.2 | `widgets/settings/upcomingActivities.qml` | 删除「滚动标题」卡片；「最多活动数」限定 1–7（RinUI SpinBox 可手动键入）；「显示活动全称」改为「显示缩写」，与存量键 `full_name` 取反绑定 |

配套 C++ 侧（不在上游同步区）：`src/core/BuiltinWidgets.cpp` 默认值改为
`max_activities=7`、`full_name=false`、删除 `marquee`。
同步上游时：本条与上游对这两个文件的差异需手工三方合并（上游仍保留 `marquee` 分支）。
```

### 4.5 翻译（`app/assets/locales/*.ts` → `*.qm`）

- 工具链可用（本机已确认）：`E:\QtMain\6.10.3\msvc2022_64\bin\lupdate.exe`、`lrelease.exe`
- 上下文：`upcomingActivities`（widget 与 settings 页共用同一上下文）
- **删除** 3 条旧文案（各 `.ts` 中对应 `<message>` 块）：
  - `Marquee Title`
  - `If enabled, the upcoming activities will scroll from left to right.`
  - `Show full name of the activities`
- **新增** 2 条：
  | source | zh_CN / zh_SIMPLIFIED / zh_HK 译文 |
  |---|---|
  | `Show abbreviation` | `显示缩写` |
  | `Use the abbreviation set for each subject, or its first character when unset` | `优先使用课程设置的缩写；未设置时取课程全称的第一个字` |
  （其余语种 en_US / it / ja_JP / lzh / ta 暂留英文原文兜底，可后续补）
- 重新编译：`lrelease app/assets/locales/zh_CN.ts -qm app/assets/locales/zh_CN.qm`（三个 zh_* 同理）
- 校验：把改过的 `.ts`/`.qm` 同步到 `dist/assets/locales/` 后再跑冒烟/手动验证

---

## 5. 配置兼容性

`WidgetsModel::loadPreset` 会「先合 `defaultSettings`，再用存量 settings 覆盖」，因此：

| 用户状态 | 升级后表现 |
|---|---|
| 存量配置 `full_name: true`（旧默认，显示全称） | **仍显示全称**，最多 `max_activities` 节（旧的 5 或新的 7），不再滚动 |
| 存量配置 `full_name: false` + 逐科设了缩写 | 显示各科缩写（语义不变），宽度自适应、最多 7 节 |
| 存量 `marquee: true` | 被忽略，永不滚动 |
| 新添加的组件 | 默认显示缩写、最多 7 节 |
| 存量 `max_activities: 5` | 保留 5，用户可在设置里改成 1–7 |

---

## 6. 验证清单

环境（本机已核实）：Qt `E:\QtMain\6.10.3\msvc2022_64`、Ninja 构建目录 `Class_Widgets_Next\build`（Release）、已部署运行目录 `Class_Widgets_Next\dist`（含 `platforms/qoffscreen.dll`）。

1. 编译：`cmake --build build --config Release`（C++ 侧只改 `BuiltinWidgets.cpp`，QML 是运行时加载，不参与编译）
2. 部署：`cmake --install build --prefix dist`，再把改动的 `app/src/qml/...` 与 `app/assets/locales/*` 同步到 `dist/`（QML/翻译是运行时文件）
3. 冒烟（抓 QML 语法/绑定错误）：
   `dist\ClassWidgetsNext.exe --smoke-test` → 期望 exit 0；日志出现 `[smoke-test] MainInterface.qml loaded and widgetsLoader connected`
   （有 `QWARN`/`QML ... is not a type` 之类即为失败）
4. 手动核对（`dist\ClassWidgetsNext.exe`，准备 5–7 节当天后续课程）：
   - 打开「即将上课」右键 → 编辑：
     - 「滚动标题」卡片已消失；
     - 「最多活动数」只能输入 1–7；
     - 「显示缩写」开关可切换；
   - 关闭缩写：显示全称，宽度随内容增长，**无滚动**；把数字框设 7 时最多 7 节；
   - 开启缩写：无缩写设置的课程显示首字（地理→地、历史→历），设了「现代」的显示「现代」；宽度同样自适应；
   - 把窗口缩到很窄 / 缩放 200%：课程从末尾被裁掉，**没有半个课程名/省略号**（除非单条就超宽）；
   - 迷你模式切换：宽度/字号过渡正常，不抖动；
   - 老配置（`marquee: true`、`full_name: true`、`max_activities: 5`）升级后：显示全称 5 节、不滚动；
   - 切换主题（default/cw1/material/vista/win10）后行为一致
5. 回归：`dynamicNotification.qml` 的设置弹窗（复用同一 settings 页）仍能打开、能保存（它不读这几个键）

---

## 7. 风险与实现注意事项

1. **上游同步纪律**：两个 QML 属于上游同步区，必须同时改 `QML_MODIFICATIONS.md`；日后同步上游需对这两个文件做三方合并。
2. **位置管理器行为假设**：`Row`/`Repeater` 对 `visible: false` 的子项不参与布局。为不依赖该假设，草案里额外把隐藏项 `width` 置 0，双保险。首次运行若发现宽度不收缩，先查这里。
3. **TextMetrics 不参与布局**：`TextMetrics` 是 QObject 而非 Item，不进入 `contentArea.childrenRect`，不会撑宽组件。
4. **字号动画**：`Title.px` 有 400ms `Behavior`；delegate 的 `onImplicitWidthChanged` 逐帧触发 `refit()`（0ms Timer 合并），过渡期间宽度会跟着动画收敛，属预期。
5. **`full_name` 键语义反转**：键名与 UI 含义相反，代码里必须留注释（已在草案中标注），避免后续误改。
6. **共享设置页**：`dynamicNotification` 仍复用 `widgets/settings/upcomingActivities.qml`，它会看到这两项（现状如此，本次不改）。
7. **QML 无法编译期校验**：所有 QML 错误只在运行时暴露，务必跑 `--smoke-test`。

## 8. 可选后续项（不在本次范围）

- 给 `dynamicNotification` 单独做一份（空）设置页，或干脆清空其 `settingsQml`（`BuiltinWidgets.cpp` 一行），消除"动态通知也显示活动节数/缩写"的无意义选项。
- 其余语种（it / ja_JP / lzh / ta）补齐新增文案翻译。
- 把 `maxActivities` 的 7 与宽度上限抽成可配置项（当前写死）。
