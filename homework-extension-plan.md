# 当日作业扩展功能 · 详细计划（已实施）

> 来源：`当日作业扩展功能计划.txt` 草拟 10 条 + 代码库实勘定稿。
> 状态：**已实施**（2026-10-03，P0–P3 全部落地；实施落点与偏差登记见
> [QML_MODIFICATIONS.md](QML_MODIFICATIONS.md) 改动 20）。
> 参考先例：`extensions-feature-plan.md`（扩展框架，已实施）、
> `four-plugins-to-extensions-plan.md`（四插件移植，已实施）。

## 0. 架构决策

| 决策 | 结论 | 依据 |
|---|---|---|
| 形态 | 第 5 个官方扩展 `classwidgets.ext.homework` | 仿 `schedulePeek`：`Class_Widgets_Next/src/core/extensions/ExtensionManager.cpp:40-81` 追加注册，`app/src/qml/ClassWidgets/pages/settings/Extensions/Index.qml:28-52` 自动渲染 |
| 展示载体 | 独立浮窗 `HomeworkFloat.qml`（`QQW.Window`），默认屏幕右侧 | 唯一支持自由拖动+调整大小的模板是 `app/src/qml/ClassWidgets/Windows/RollCallResult.qml:7,246-376`（整卡拖动+右下 18px 缩放手柄+`persistGeometry`）；`WidgetsContainer` 仅组内排序、无自由定位/缩放 |
| 视觉 | 外壳用 `ClassWidgets.Theme` 的 `Widget{}` | `app/src/qml/ClassWidgets/Theme/components/Widget.qml:7` 自带亮暗+主题覆盖，与顶部组件一致 |
| 状态源 | 只读 `AppCentral.scheduleRuntime`，心跳复用 `UnionTimer`，不自开秒级 `QTimer` | `src/core/schedule/UnionTimer.h:12-23`；`ScheduleRuntime.cpp:312-318` |
| 通知 | 复用 `NotificationService/NotificationProvider` | `src/core/notification/NotificationService.cpp:180-226` + `NotificationProvider.cpp:52-73`，QML（`dynamicNotification.qml:187-209` / `FloatingWidget.qml:58-84`）自动覆盖显示 |
| 科目开关 | `subject.needsHomework: bool` 默认 `true`，存量幂等补齐 | 范本 `isLocalClassroom`：`pages/editor/Subjects.qml:149-159` + `src/core/schedule/ScheduleEditor.cpp:224-277` |

## 1. 数据模型与配置

### 1.1 作业数据：独立按天文件（定稿）

* 路径：`app/configs/homework/YYYY-MM-DD.json`（仿 `app/configs/schedules/` 的 `ScheduleIO` 模式）。
* 单文件形状：`{"date":"2026-10-03","items":[{"id":"uuid","subjectId":"geo","content":"练习册P4~P6","priority":"none|orange|blue|green","updatedAt":1234567890}]}`。
* 不写入 `configs.json`，避免膨胀。新 `HomeworkService`（仿 `RollCallService`，挂 `AppCentral` 上下文）负责按天读写、日期过滤、启动/跨天清理过期文件。
* 保留时长：`extensions.homework.retention_days`，Enum `1/3/7`，默认 `7`，设置页 `ComboBox` 可调（抄 `SchedulePeek.qml:37-51` 范式）。清理规则：保留最近 N 天文件，超期删除。

### 1.2 `ConfigStore` 小键（`src/core/ConfigStore.cpp` 白名单+默认树+校验三处）

```json
"extensions.homework": {
  "delay_minutes": 0,
  "locked": true,
  "window_x": null, "window_y": null, "window_w": 320, "window_h": 400,
  "notify_enabled": true,
  "auto_show": true,
  "retention_days": 7
}
```

* `delay_minutes`：Int，钳位 `0~10`（定稿范围），默认 `0`。
* `locked`：Bool，默认 `true`。
* `retention_days`：Enum `1/3/7`。

### 1.3 课表侧

* `subject.needsHomework: bool` 默认 `true`；`ScheduleModel.cpp:278-283` 旁追加归一化补齐；`defaultSubjects:412-463`、`ScheduleConverter.cpp:487-494,1459-1477` 同步；`ScheduleEditor.h:52-63` 的 `addSubject/updateSubject` 尾参 `bool needsHomework=true`。

## 2. 功能详述

### F1 触发与显隐（含拖堂延迟）

* 下课定义：`currentStatus ∈ {break, free}` 且刚从 `class` 切出。不含 `preparation/activity`。参考 `ScheduleRuntime.cpp:129-150` + `SchedulePeekBar.qml:176-199`。
* 放学后最后一段 `free`：正常弹出（定稿）。
* 延迟：单发 `Timer`，下课时 `restart(delay*60*1000)`；运行期间切回 `class/preparation` 则 `stop()` 并隐藏。连续课间 < delay 则本次不弹。
* 显隐：timer 到期 → `WindowManager.openHomeworkFloat()`；`status==class||preparation` → 自动隐藏；手动关闭仅隐藏本次；`auto_show==false` 则永不自动弹。
* `needsHomework==false` 的课下课：浮窗照常显示，不抑制（定稿，仅抑制通知）。

**实施增补（原计划未列，均为实测补齐）**：

* 延迟到期时**复核** `currentStatus` 仍为 `break/free`；调休/切课表/休眠唤醒造成的状态漂移按取消处理，不弹不播。
* 上课/预备一律收起浮窗，**不区分**自动弹出与手动打开——否则启动补开、扩展开关开启、设置页手动打开的浮窗会被标志位永久豁免隐藏，表现为「常驻显示」。
* `activity`（自习/活动段）不算下课沿（`currentStatus` 须为 `break/free`），也不取消已排期的延迟定时器；`break↔free` 之间不重复触发。
* 通知去重指纹「日期|上节课id|endTime」**只作用于通知**：浮窗触发不套指纹，否则「拖堂后再次下课」（同 id 同 endTime）会把浮窗一并吞掉。

### F2 右侧浮窗

* 新建 `app/src/qml/ClassWidgets/Windows/HomeworkFloat.qml`，抄 `RollCallResult.qml:56-99,206-264,346-376`：`Frameless|StaysOnTop|Tool`，首启右侧垂直居中（抄 `FloatingWidgetContainer.qml:73-89` + `RollCallFloat.qml:91-102`），右下 18px 缩放手柄（min 240x200，钳屏），`persistGeometry` 双检 `isKeyLocked`，多屏负坐标抄 `RollCallResult.qml:66-75`。
* `AppWindowManager` 新增 `open/closeHomeworkFloat` 单例 + `windowQmlPath` 映射。内容区 `Widget{}` + 标题栏 + `ListView` + 底部 `+` 行。

**实施增补**：拖动收窄到**标题栏**而非整卡——卡片主体是 `ListView`（`Flickable`），祖先级 `DragHandler` 会与列表滚动手势抢指针（整卡拖动会让列表滚不动）。

### F3 灵动通知

* 文案：标题 `作业布置`，正文 `请{subjectName}课代表填写当日作业`；取不到科目则 `请各科课代表填写当日作业`。科目名经 `scheduleRuntime.subjects` 查 `name`（`upcomingActivities.qml:59-85` 范式）。
* 上节课获取：QML 现算 `currentDayEntries` 过滤 `type==class && end<=nowMinutes` 取 `end` 最大者（抄 `SchedulePeekBar.qml:188-199`）。
* 时机：`delay==0` 时在下课铃通知后约 600ms 再发（QML 新通知覆盖旧通知，无队列堆积）。去重指纹 `日期|上节课id`（仿 `m_lastStatusKey:240-248`）。
* 抑制规则（定稿）：上节课科目 `needsHomework==false` 或 `notify_enabled==false` 则不发通知；浮窗不受影响。
* `delay>0` 时通知随浮窗同时发（不再等 600ms）。

### F4 锁定/解锁 + 设置页

* 新建 `pages/settings/Extensions/Homework.qml`（抄 `SchedulePeek.qml` 三件套）：总开关走 `Extensions.setEnabled`；`Switch` 锁定/解锁→`extensions.homework.locked`；`SpinBox/Slider` 延迟 0~10；`Switch` 自动展示/灵动通知；`ComboBox` 保留 1/3/7 天；作业管理入口。全部 `enabled:!isKeyLocked` + `Connections onDataChanged` 刷新。
* `locked==true` → 禁拖动+隐藏缩放手柄；另叠加 `isKeyLocked("extensions.homework.window_x")` 双 gate。
* 「打开作业浮窗」入口受总开关约束，否则禁用扩展后仍可从本页打开并使用完整编辑功能。

### F5 双入口编辑

* 新建 `Components/dialogs/HomeworkEditDialog.qml`（`RinUI Dialog + Ok|Cancel + onAccepted`，抄 `RescheduleDayDialog.qml:74-92`）：科目下拉（抄 `EntryDetailView.qml:181-217`，过滤 `needsHomework!==false`）+ 正文必填 + 优先级四选一。经 `HomeworkService` 写当天文件。
* `+` 号行恒在列表末尾；空列表时即第一行；点击预填上节课科目后开对话框。
* 入口统一 `openFor(itemId, subjectId, content, priority)`：`itemId` 空即新建，其余回填；打开时刷新科目下拉快照（避免编辑期间课表推送重建模型打断选择）。

### F6 列表渲染

* `ListView{model: todayItems}`，delegate 高度自适应，`Text.Wrap`，文本 `<b>{subject}：</b>{content}`；多行同项 `currentIndex` 即选中（抄 `AddWidgetsDialog.qml:15-17`）。
* 优先级仅整行字体变色：`orange=#E67E00 / blue=#1E90FF / green=#2E9E5B / none=默认`；不做色盲模式（定稿）。空状态提示点击 `+` 添加。

### F7 选中 → 编辑/删除

* 选中后右下角浮现编辑/删除二键（避开缩放手柄热区，编辑态隐藏手柄）；删除经 `Dialog(Ok|Cancel)` 确认，文案 `即将删除一条作业，此操作不可撤销，是否继续？`（抄 `PluginReplaceConfirmDialog.qml:7-19`）；编辑复用 F5 对话框回填。

### F8 课表编辑器改造

* `Subjects.qml:13 openEditDialog` 加形参 + `:149-159` 照抄 `Held in homeroom` 新增“需要布置作业”（默认开）+ `:189-193` 回写；`SubjectClip.qml:11-12,18-25` 透传。作业下拉过滤 `needsHomework!==false`。

## 3. 文件清单（新建 ✚ / 修改 ✎）

* ✚ `Windows/HomeworkFloat.qml`、`Components/HomeworkTrigger.qml`、`Components/dialogs/HomeworkEditDialog.qml`、`pages/settings/Extensions/Homework.qml`、`src/core/extensions/HomeworkService.{h,cpp}`
* ✎ `ExtensionManager.cpp` 注册；`ConfigStore.cpp` 白名单+默认树+校验；`ScheduleModel.{h,cpp}` + `ScheduleEditor.{h,cpp}` + `ScheduleConverter.cpp`；`Subjects.qml` + `SubjectClip.qml` + `MainInterface.qml` + `Components/qmldir`；`AppCentral.{h,cpp}` + `AppWindowManager.*`；`WidgetsWindow.cpp`；通知分支（`HomeworkService::notify` 经 `NotificationProvider`，provider `com.classwidgets.homework`）；8 语种 `.ts` + `lrelease`。

## 4. 分期与验收

* P0：文件 IO + `retention_days` + 注册 + 设置页空壳（落盘/重启保持/`isKeyLocked`）→ `CMake 全量 + offscreen --smoke-test + qmllint 零 error`。
* P1：浮窗+拖动缩放+锁定+只读列表+自动显隐（含 delay cancel）。
* P2：编辑/删除/确认弹窗+通知+课表开关+过滤。
* P3：优先级颜色+空状态+多屏+主题/深浅/i18n。
* 回归：上课隐藏自动化、速览条 `auto`、点名窗、托盘通知不被覆盖；过期文件清理（1/3/7 天）实测。

## 5. 数据安全约定（实施增补）

`HomeworkService` 的按天文件读取分两路，均在 `src/core/extensions/HomeworkService.h` 注释登记：

* **展示路**（`readDayFile`）：`Missing`（文件不存在，按空）/ `Ok` / `Corrupt`（存在但非法）。`Corrupt` 时**不得**静默覆写。
* **写路**（`loadDayForWrite`）：解析成功即字段级自愈（补缺失 `id`、规范化字段，条目不丢）；损坏则先备份到 `corrupt-*` 再按空处理。**留底失败时必须放弃本次写入**——宁可拒写，也不能把用户当日数据覆盖掉。
* `sanitizeItems(displayOnly=true)` 额外剔除空正文条目，仅影响展示。

## 6. 时钟口径

`HomeworkService::currentDate()` 与 `ScheduleRuntime`/通知指纹同源，叠加 `schedule.time_offset`（单位秒）；调试改期时保证作业文件日期与通知链路同天。QML 侧 `HomeworkTrigger.currentMinutes()` 同样叠加 `runtime.timeOffset`。