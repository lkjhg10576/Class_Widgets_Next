# QML 修改记录（上游同步区纪律）

> 移植方案 §9.5：QML 目录是**上游同步区**——能不改就不改，改动必须集中记录在本文档，
> 并定期（建议每月）从上游 `main` 拉取 `src/qml/` 变更做回归。

## 当前状态：`app/src/qml` 共 13 处改动（4 处 M3 插件入口遮蔽 + 2 处 M5 方法名改写 + 2 处 Interactions 页 sourceSize 移除 + 1 处 WidgetLoader 失败恢复 + 2 个「即将上课」组件文件 + 1 处 AddWidgetsDialog 数量上限 + 2 处扩展功能接入（Settings 导航项 / WidgetsContainer 速览条挂载，改动 12），见下；另有改动 13–17 增量与改动 18 质检修正、改动 19 点名交互修复、改动 20 当日作业、改动 21 语音播报见各节）
> 另有 16 个**新增文件**（非上游改动）：天气小组件及其设置页（改动 5）、
> 倒数日小组件及其设置页（改动 9）、扩展功能 7 个 QML（改动 12），
> 四插件移植新增 3 个 QML（改动 17：DisplayTweaks 页 / AddOverlayMemberDialog / LessonsBoard，RollCall/Weather/Peek 系为既有文件增量），
> 当日作业新增 4 个 QML（改动 20：HomeworkTrigger / HomeworkFloat / HomeworkEditDialog / Homework 设置页），
> 语音播报新增 1 个 QML（改动 21：Tts 设置页；该扩展在上游同步区**改动 0 处**）。
> `app/src/themes/**` 不再逐字同步上游：改动 7（material Color.qml，A6 引入、本版修正）
> 与改动 8（WidgetLoader.qml 失败恢复，属 `app/src/qml`）已偏离上游。

| 目录 | 内容 | 与上游的差异 |
|---|---|---|
| `app/src/qml/` | 上游 `src/qml/` 全量（137 个 QML、8 个 qmldir，HEAD `1e66f09`） | **多处（见改动 3、4、6、8、9、10、11、12、17、20）** |
| `app/src/themes/` | 上游内置主题定义 | **1 处（material Color.qml，改动 7；上游原样保留在文件外注释）** |
| `app/assets/` | 上游资产 | **无（逐字复制）** |
| `app/themes/` | 上游外部主题扫描目录 | **无（逐字复制）** |
| `app/examples/` | 上游示例课表 | **无（逐字复制）** |
| `app/RinUI/` | 官方 Windows 发行包（v2.0.1.0）内附的 RinUI QML 库（117 文件） | **1 处，见下** |

## 已记录的改动

### 1. `app/RinUI/components/Navigation/NavigationSubItem.qml`（vendored 覆盖）

上游在 `src/qml/RinUI/` 下只保留了一个文件——`components/Navigation/NavigationSubItem.qml`，
作为对 PyPI RinUI 同名组件的**局部覆盖补丁**，依赖「`src/qml` 在导入路径中优先于 RinUI 库
路径」生效（`central.py:409`）。

C++ 版处理方式（双保险）：

1. `RinUiWindowBase` 按相同优先级注册导入路径：`qmlRoot()`（src/qml）最先，
   vendored `RinUI/` 兜底（`src/core/RinUiWindowBase.cpp`，有注释标记，勿改顺序）；
2. vendor `RinUI/` 时直接用该补丁文件覆盖了同名文件——即使导入路径机制与
   pip 版 RinUI 的解析行为有差异，补丁内容也一定生效。

### 2. 已知未参与构建的 QML（无需改动，仅备忘）

- 插件专属页面（Phase 2 才恢复，本仓库保留文件；其入口已在 M3 按改动 3 遮蔽）：
  `pages/plaza/*`、`pages/settings/Plugins.qml`、`pages/tutorial/Plugins.qml`、
  `ClassWidgets/Plugins/*`、`Components/dialogs/PluginReplaceConfirmDialog.qml`
- `pages/tutorial/Welcome.qml` 依赖 `QtQuick.VectorImage`（Qt ≥ 6.9，跟随 6.10 无影响）

### 3. M3 插件入口遮蔽（4 处，移植方案 §0.5.8）

M3 窗口管理落地时，`WindowManager.openPlaza()` 实现为日志警告 + no-op；
为避免用户点进不可用的插件入口，同时把 QML 里的可见插件入口注释掉。
四处均为**纯注释**（上游内容原样保留在注释里，Phase 2 取消注释即恢复）：

| # | 文件 | 位置 | 改动 |
|---|---|---|---|
| 3.1 | `ClassWidgets/Windows/Settings.qml` | 导航模型 `Plugins` 条目（原 76-82 行） | 注释掉 `{ title: "Plugins", page: pages/settings/Plugins.qml, ... subItems: UtilsBackend.extraSettings }` 整个导航条目 |
| 3.2 | `ClassWidgets/pages/settings/Home.qml` | 快捷设置 Repeater model（原 145 行） | 注释掉 `{ icon: ..., title: "Plugins", page: "Plugins.qml" }` 卡片条目 |
| 3.3 | `ClassWidgets/Windows/Tutorial.qml` | `pageUrls` 数组（原 134 行） | 注释掉 `../pages/tutorial/Plugins.qml` 一项；后续步骤从 Preferences（页内硬编码 `currentStep: 5, totalSteps: 6`）直达 `Complete.qml`。已知外观影响：步骤指示仍按页内 `totalSteps: 6` 显示（"Step x of 6"），第 6 步无指示页 |
| 3.4 | `ClassWidgets/Data/WhatsNewData.qml` | "Enhanced Plugin System" 卡片（原 37-38 行） | 注释掉 `actionButtonText: "Visit Extension Plaza"` 与 `actionButtonAction: "openPluginPlaza"` 两行，卡片正文保留 |

对应 C++ 侧行为（`src/core/windows/AppWindowManager.cpp`）：
`openPlaza()` / `openPluginPlaza()` / `closePlaza()` 均为警告 + no-op；
`pages/plaza/*`、`Windows/PluginPlaza.qml`、`pages/tutorial/Plugins.qml` 不参与本阶段构建。

### 4. M5：`ScheduleClip.qml` 两处方法名改写（C++ 关键字限制）

| # | 文件 | 位置 | 改动 |
|---|---|---|---|
| 4.1 | `ClassWidgets/Components/editor/ScheduleClip.qml` | 原 146 行 | `AppCentral.scheduleManager.export(filename)` → `exportSchedule(filename)` |
| 4.2 | 同上 | 原 287 行 | `AppCentral.scheduleManager.delete(filename)` → `removeSchedule(filename)` |

原因：`export` / `delete` 是 C++ 关键字，moc 无法为同名方法生成 Q_INVOKABLE 注册代码
（与 QTBUG-5426 同类的语言级限制）。C++ 侧 `ScheduleManager` 的入口本就是
`exportSchedule` / `removeSchedule`（语义与上游 `export` / `delete` 完全一致）。
同步上游时：若上游修改这两个调用点的**参数**，需手工映射到新方法名。

### 5. 天气小组件（2 个新增文件，本移植新增内置组件）

上游 CW2 无内置天气组件；本移植按用户要求以原生功能新增（数据源小米天气
wtr-v3，实现移植自用户另一项目 NetSpeed-Dynamic 的 `src-tauri/src/weather.rs`，
C++ 侧为 `src/core/weather/WeatherService.*`）。因是**新增文件**而非上游文件改动，
上游同步不会冲突；若上游未来新增同名路径需在此复核。

| # | 文件 | 性质 | 说明 |
|---|---|---|---|
| 5.1 | `widgets/weather.qml` | 新增 | 内置组件 `classwidgets.weather`；backend 为注入的 WeatherService（唯一非通用 backend 的内置组件）。2026-10 天气迁移见下方追记 |
| 5.2 | `widgets/settings/weather.qml` | 新增→**退役** | 城市搜索（350ms 防抖）+ 刷新间隔（全局键 `weather.poll_interval`，秒）+ 数据源标注。2026-10 天气迁移中被扩展页取代，见下方追记 |

**追记（2026-10-01，阶段 B 天气迁移，extensions-feature-plan.md §5）**：
`widgets/weather.qml` 城市读取从每实例 `settings.city` 改为全局 `weather.city`
（JSON 字符串，键由 ConfigStore 默认树保证存在），空态两处提示文案改指
「扩展功能-天气」页（`Set a city / Set API key in Extensions - Weather settings`）；
`widgets/settings/weather.qml` **退役**——`BuiltinWidgets.cpp` 天气定义不再设置
`settingsQml`，右键设置入口因 `model.settingsQml` 为空而禁用，全仓库对该文件的
引用归零（文件头部注释已声明退役与复活条件）。两文件均为项目自有新增文件而非
上游文件，改动不构成上游同步冲突面。

### 6. 两个 Interactions 页移除预览图 `sourceSize`（修复引导第 4 步整进程卡死）

A7 曾给 12 处大图加"按显示尺寸 × DPR"的 `sourceSize`；其中两个 Interactions 页
（引导 `pages/tutorial/Interactions.qml`、设置 `pages/settings/General/Interactions.qml`）
的预览图（`hide_*.png`，321×225 / 8–18KB，本属 A7 应跳过的非大图）不该加：

- 图片宽度来自 `Layout.fillWidth` 布局，而布局又受图片隐式尺寸影响（SettingItem
  内容自适应 + Expander implicitHeight 链）；`sourceSize` 绑定 `width` 后，异步解码
  结果改变隐式尺寸 → 布局重排改变 width → sourceSize 变化击穿解码缓存再次解码，
  形成**无限"重解码-重排"振荡**（探针实测宽度 94↔97 乒乓、每循环一次新解码），
  GUI 线程 + 图片解码线程双满载，事件循环饿死 → 走引导到"选择小组件的交互方式"
  一步整进程无响应。
- 修复 = 删除该 `sourceSize` 绑定（保留 A7 注释位说明原因）。图片很小，按原生
  分辨率解码无内存代价；对照实验（保留 fillWidth、仅去 sourceSize）页面正常。
- 其余 `sourceSize` 用点复核过：8 处 `anchors.fill`（宽度与隐式尺寸无关，安全）；
  `Windows/WhatsNew.qml` 一处为 fillWidth/fillHeight + maximumHeight，当前结构
  稳定，暂不动。

### 7. material 主题 `Color.qml`：信号监听经 property 声明挂载（material/vista 主题失效修复）

A6 把上游 200ms 轮询 Timer 改写为 `Connections { target: Utils ... }` 时，把它当成了
`QtObject` 根的**子对象**声明。`QtObject` 没有 `data` 默认属性，子对象声明在引擎
实例化时直接报"无法分配给不存在的默认属性"——`MaterialColor` 单例整个不可用，
进而 `ClassWidgets.Theme.Material` 的所有使用方（Text/Icon/Widget…）类型不可用，
Material You 主题下所有小组件 Loader.Error、整主题回退默认。

修复（本版）：改为 `property Connections _utilsWatcher: Connections { ... }`，
经属性声明挂载（QtObject 允许属性、不允许子对象），保留 A6 的信号驱动语义。
连带修复：该失败曾触发 WidgetLoader.qml 的 reloading 卡死（见改动 8），使同会话内
先选 material 失败后、再选 vista（或任何其他主题）小组件永久消失——即用户报告的
"Material You 与 Vista 都完全不可用"。

### 8. `WidgetLoader.qml`：Loader.Error 时复位 `reloading`

`onThemeReadyToReload` 用 `if (reloading) return` 防重入，但 `reloading` 只在
`Loader.Ready` 分支复位——一旦某次主题重载以 Error 告终（如改动 7 的 material），
该 Loader 对后续所有 `themeReadyToReload`（含失败回滚默认主题）永久跳过，
小组件从此消失直到重启进程。修复：Error 分支同样 `reloading = false`，保证
回滚/改选其他主题时必然重试。

### 9. 倒数日小组件（2 个新增文件，本移植新增内置组件）

上游 CW2 无倒数日组件；按用户要求以原生功能新增（注册于
`src/core/BuiltinWidgets.cpp`，id `classwidgets.countdownDays`）。与改动 5 同纪律：
因是**新增文件**而非上游文件改动，上游同步不会冲突；若上游未来新增同名路径需在此复核。

| # | 文件 | 性质 | 说明 |
|---|---|---|---|
| 9.1 | `widgets/countdownDays.qml` | 新增 | 显示状态机：未配置 → 默认名 + 右键提示；未来 → `距离（标题）还有 N 天`；就是今天 → 事件名 + `就是今天`；已过（仅不重复）→ `距离（标题）已过 N 天`（正数，负数不再外露）。settings 契约 `title` + `target_date`（"yyyy-MM-dd"）+ `cycle`（"none" \| "weekly" \| "monthly" \| "yearly"，缺省 "none"）；循环型按年月日分量向前滚动到下一次发生日（小月/平年顺延至月末最后一天，避免毫秒加减跨夏令时偏差）；天数按目标日 0 点 − 今日 0 点计算，订阅 UnionTimer 秒心跳实现跨零点自动重算（A6 纪律，不自开 QTimer）；backend 用通用 WidgetBackend |
| 9.2 | `widgets/settings/countdownDays.qml` | 新增 | 事件标题（TextField，命令式初始化对齐 settings/Text.qml 先例）+ 目标日期（RinUI `CalendarDatePicker`，选择后写回 `target_date`）+ 重复周期（RinUI `ComboBox`，四档不重复/每周/每月/每年，写回 `cycle`，命令式初始化定位）；沿用 settings 整体重赋契约（Ok 时由 WidgetSettingsDialog 调 `WidgetsModel.updateSettings` 持久化） |

配套改动：`src/core/BuiltinWidgets.cpp` 注册项（名称翻译走显式 `"Widgets"` 上下文，
同 ScheduleManager 的 `QCoreApplication::translate` 用法）：`defaultSettings` 新增
`cycle`（存量无此键的配置由 `loadPreset` 以默认值补齐，无需迁移）；`maxInstances = 3`
（同一预设内最多 3 个实例，真正拦截在 `WidgetsModel.addInstance`，UI 侧见改动 11）；
`app/assets/locales/` 的 `zh_CN` / `zh_SIMPLIFIED` / `zh_HK` 三个 `.ts` 补 `Widgets`
词条与 `countdownDays` context（新增 Since %1 / Today is the day / 重复周期四档等），
`.qm` 已用 lrelease 重新生成（其余语言无中文词条，显示英文源文，与天气组件同状态）。

### 10. 「即将上课」组件：显示缩写开关 + 宽度自适应（2026-09-26）

| # | 文件 | 改动 |
|---|---|---|
| 10.1 | `widgets/upcomingActivities.qml` | 移除 `MarqueeTitle`（滚动），改为每节课一个 `Title` 排进 `Row`，宽度随内容自适应；新增缩写解析（`simplifiedName` → 全称首字）、最多 7 节、超出屏幕宽度时从末尾裁剪（保底首条 `ElideRight`）。新增 `import QtQuick.Window`（取 `Screen.width` 算宽度上限） |
| 10.2 | `widgets/settings/upcomingActivities.qml` | 删除「滚动标题」卡片；「最多活动数」限定 1–7（RinUI SpinBox 可手动键入）；「显示活动全称」改为「显示缩写」，与存量键 `full_name` 取反绑定 |

配套 C++ 侧（不在上游同步区）：`src/core/BuiltinWidgets.cpp` 默认值改为
`max_activities=7`、`full_name=false`、删除 `marquee`。
配套翻译：`app/assets/locales/*.ts` 的 `upcomingActivities` 上下文删除 3 条废弃文案
（Marquee Title / 滚动描述 / 显示全称）、新增 2 条（Show abbreviation 及其说明），
三个 zh 语种已译，其余语种暂回退英文原文。
同步上游时：本条与上游对这两个文件的差异需手工三方合并（上游仍保留 `marquee` 分支）。

### 11. `AddWidgetsDialog.qml`：实例数量上限 UI（2026-09-27）

为支撑倒数日"同一预设最多 3 个"（改动 9），引入通用的 `maxInstances` 机制。
C++ 侧（不在上游同步区）：`WidgetDefinition` 新增 `maxInstances`（0 = 不限量，
随 `definitionsList` 以 `max_instances` 键暴露给 QML）；`WidgetsModel.addInstance`
达到上限时拒绝并记 warn（只拦新增，不破坏手改配置的存量）；新增
`Q_INVOKABLE instanceCount(typeId)` 与 `instancesRevision` 属性（实例增删/预设切换
时自增，NOTIFY 沿用 `modelChanged`）供 QML 绑定刷新。

| # | 文件 | 改动 |
|---|---|---|
| 11.1 | `ClassWidgets/Components/dialogs/AddWidgetsDialog.qml` | 对话框新增 `instancesRev` 属性订阅实例变化；「添加」按钮达到上限时禁用；按钮上方新增上限提示（`Up to %1 instances`，三个 zh 语种已译）；`max_instances` 缺省按不限量处理，上限为 0 的组件行为与原来一致 |

同步上游时：本条是上游文件改动，若上游重写此对话框，需把 `instancesRev` /
`enabled` / 提示三处逻辑手工合入。

### 12. 扩展功能三扩展（2026-10-01，extensions-feature-plan.md 阶段 A–D）

新增官方「扩展功能」模块（框架 + 天气迁移 / 随机点名 / 课表速览三扩展）。
C++ 实现全部在同步区外；本节登记**上游同步区改动 2 处**（12.1、12.2）与
**新增 QML 文件 7 个**（12.3–12.9，按改动 5/9 纪律：新增文件不与上游冲突，
上游若出现同名路径需复核）。扩展与插件严格分离：不复用 `plugins.*` 键、
`Plugins.qml` 页与 `PluginManagerStub`，也不加载任何第三方代码。

**改动（上游同步区，2 处）**：

| # | 文件 | 位置 | 改动 |
|---|---|---|---|
| 12.1 | `ClassWidgets/Windows/Settings.qml` | `navigationItems` 数组，「个性化」条目之后（行内注释 `[CWN-EXT-A4]`） | 插入导航项 `{ title: qsTr("Extensions"), icon: "ic_fluent_puzzle_cube_20_regular", page: pages/settings/Extensions/Index.qml }`（计划 §4 A4）。文案源文为英文 `Extensions`，各语种译文已进 .ts。同步上游：纯增量插入，若上游重排 `navigationItems` 把该项放回「个性化」之后即可 |
| 12.2 | `ClassWidgets/Components/WidgetsContainer.qml` | 根 `Column` 内、`widgetsFlow` 之后、`addWidgetsContainer` 之前（行内注释 `[CWN-EXT-D3]`） | 插入 `SchedulePeekBar { id: schedulePeekBar }`（组件在 Column 自然排布下贴于小组件组正下方，hide/anchor 偏移随 Column 总高自动跟随），并新增 `Connections`：速览条 `visible/width/height` 变化即发射既有信号 `contentGeometryChanged()`，把速览条弹出/收起接入「MainInterface → WidgetsWindow 重算蒙版」的既有几何链路（复用触发链、不新增直连、不改既有发射点）。计划 §7 D3。同步上游：插入点以 `widgetsFlow` 闭合为锚，纯增量 |

**新增（QML，7 个）**：

| # | 文件 | 说明 |
|---|---|---|
| 12.3 | `ClassWidgets/pages/settings/Extensions/Index.qml` | 扩展列表页：渲染 `ExtensionManager` 注册表（图标/名称/描述 + Switch 开关写回 `extensions.enabled` +「设置」跳子页，跳页写法照 `Home.qml` 先例）。计划 §4 A3 |
| 12.4 | `ClassWidgets/pages/settings/Extensions/Weather.qml` | 天气扩展配置页：由 `widgets/settings/weather.qml` 迁移改造——城市搜索/选择（读写全局 `weather.city`）+ 数据源 + API 凭据 + 刷新间隔四张卡。计划 §5 B5 |
| 12.5 | `ClassWidgets/pages/settings/Extensions/RollCall.qml` | 随机点名配置页：名单管理（TXT 导入经 `RollCallService.importNamesFromUrl`、手动添加、改名、删除、权重滑杆 -100~+100）+ 重复策略（单次内/会话内不重复）。计划 §6 C2 |
| 12.6 | `ClassWidgets/pages/settings/Extensions/SchedulePeek.qml` | 课表速览配置页：显示模式（`extensions.schedule_peek.mode` auto/always）+ 分组间隔阈值滑杆（`split_gap_minutes`，5~60）。计划 §7 D5 |
| 12.7 | `ClassWidgets/Components/SchedulePeekBar.qml` | 速览条本体：当日课表格（`objectName: "schedulePeekBar"`，C++ 蒙版 `findChild` 依赖）——课程缩写（`subject.simplifiedName` 首字，未设取全名首字）、相邻间隔 ≥ 阈值插分组竖线、当前课橙色/下一课绿色圆形高亮；auto 模式下课弹出上课收起（显示条件含 `futureCount ≥ 1` 实现放学后隐藏）、always 常驻、当天无课隐藏。数据全部读 `ScheduleRuntime` 既有属性，零新增 C++ 接口。计划 §7 D1/D2 |
| 12.8 | `ClassWidgets/Windows/RollCallFloat.qml` | 悬浮点名窗：无边框置顶透明 `Window`，圆形「点名」按钮可拖动（钳位/持久化照 `FloatingWidgetContainer.qml` 先例，位置键 `extensions.roll_call.button_x/y`，-1=选中屏右上角默认）、点击展开 点1/2/3名+取消 面板并按按钮所在屏幕半区向左/右展开。计划 §6 C3 |
| 12.9 | `ClassWidgets/Windows/RollCallResult.qml` | 点名结果窗：居中于 `preferences.display` 选中屏幕，大字号列出抽中名字，「再点 1/2/3 名」+「关闭」；关闭时清空会话排除名单（session 策略）。计划 §6 C4 |

配套改动（C++，同步区外，登记于此供追溯）：新增
`src/core/extensions/ExtensionManager.{h,cpp}`（注册表 + `extensions.enabled` 读写 +
`Extensions` QML 上下文 + B3 一次性老配置迁移）与
`RollCallService.{h,cpp}`（名单/加权抽取/TXT 解析，`importNamesFromUrl` 纯解析无副作用，
写回在设置页完成），二者加入 `CMakeLists.txt`；改动 `AppCentral.{h,cpp}`（装配、上下文
注册、`rollCall` 属性、天气轮询随开关 start/stop）、`ConfigStore.cpp`（`extensions.*`
进 defaultConfig 与 `kScalarSpecs`，`avoid_repeat`/`mode` 用枚举白名单收紧）、
`WidgetsModel.cpp`（`definitionsList` 未启用时过滤天气定义；天气 `WidgetDefinition`
`maxInstances=1`、`settingsQml` 不再赋值，见 `BuiltinWidgets.cpp`）、
`AppWindowManager.{h,cpp}`（`RollCallFloat`/`RollCallResult` 两窗口接入 + 开关接线）、
`WidgetsWindow.cpp`（`updateMask()` 并入速览条矩形，计划 §7 D4）。

配套翻译：`app/assets/locales/*.ts` 8 语种各新增 99 条（`Extensions`（C++ 名称/描述）、
`Weather`、`RollCall`、`SchedulePeek`、`RollCallFloat`、`RollCallResult`、`Index`、
`Settings`、`weather` 九个 context；en_US 中文源条目给出英文译文、纯英文源条目回退，
it/lzh/ta 暂空回退源文），`.qm` 已用 lrelease 重新生成（2026-10-01）。

## 改动 13：扩展功能三修复（2026-10-01）

用户反馈的三个问题（点名操作无反馈 / 名单列表位置 / 天气不刷新），涉及改动 12 的
两个新增文件与既有 `widgets/weather.qml`。

| # | 文件 | 改动 |
|---|---|---|
| 13.1 | `ClassWidgets/pages/settings/Extensions/RollCall.qml`（改动 12.5 的增量） | ① **操作反馈**：导入 TXT / 手动添加 / 清空 / 改名 / 权重提交 / 删除的结果由原来只写「手动添加」卡片描述行（Caption 小字，长页面里易被忽略）改为 `floatLayer.createInfoBar(...)` 浮层（成功 4s 自动关闭，失败以 Error 配色醒目弹出）；文案全部复用本页既有 `.ts` 条目，零新增可翻译字符串；② **名单列表**：逐行改名/权重/删除的 `Repeater` 从「手动添加」之后移到「重复策略」**下方**，并补列表段标题行（「名单」+ 右侧 `共 %1 人`）与空名单引导行，行标题带 `序号.` 前缀 |
| 13.2 | `widgets/weather.qml` | 新增 `onBackendChanged`（`WidgetLoader` 在 `Loader.Ready` 后才注入 backend，晚于本项 `Component.onCompleted`，此前首次拉取要等 `cityJson` 二次变化才触发）与 `Connections.onConfigChanged`（数据源/凭据变更后立即重读 `weatherData()`，反映新源的 `unconfigured` 等解释态）；拉取逻辑抽为 `requestFromBackend()` 供两处复用 |
| 13.3 | `ClassWidgets/pages/settings/Extensions/Weather.qml`（改动 12.4 的增量） | 数据源切换、凭据（key/host）提交、城市选定三处均调用 `AppCentral.weather.applyConfigChange()`，改动即时生效（原来切源后要等组件自身 request 或 60s 轮询） |

配套 C++ 改动（同步区外，登记供追溯）：`WeatherService::start()` 增加启动即拉取
（读 `weather.city` 登记活动城市并 `fetchNow`，不再依赖 QML 组件的加载时序）；
`applyConfigChange()` 先把 `weather.city` 并入活动城市再逐个 `manual` 重拉，并新增
`configChanged()` 信号供消费者刷新解释态；新增私有辅助 `configuredCity()`。
`scripts/build-installer.ps1` 的 ISCC 定位增加本机 D 盘候选路径；版本号升至 2.1.0.2。

## 改动 14：随机点名 TXT 导入大名单内存暴涨/假死修复（2026-10-01）

用户反馈：名单人数较多时经 TXT 导入后内存持续走高、软件整体无响应卡死。

**根因**：导入按「解析结果逐名 `addName`」合入。每次 `addName` 都会
`ConfigStore::set` → 整树 `dataChanged` → 设置页 `Repeater`（模型绑定
`Configs.data.extensions.roll_call.names`）全量重建每人一张 `SettingCard`，并
整份 `configs.json` 落盘；导入 N 人到现有 M 人名单是 O(N·(M+N)) 次 QML 委托
创建与 N 次全量磁盘写。整个循环还在同一事件循环回合内执行，QML 延迟销毁与
GC 无从运行 —— 被替换的旧委托持续堆积，内存随导入线性攀升直至卡死/OOM。
班级规模（几十人）下每轮重建量小，问题直到大名单才暴露。

**修复**：

| # | 文件 | 改动 |
|---|---|---|
| 14.1 | `src/core/extensions/RollCallService.{h,cpp}`（同步区外；改动 12 配套 C++ 的增量） | 新增 `Q_INVOKABLE int mergeNames(const QVariantList &entries)`：`importNamesFromUrl` 解析结果的批量写入口——一次完成存量/批内去重（空名/重名跳过）与权重缺省 0、钳位 [-100,100]，单次写回 + 单次 `save()` + 一次 `namesChanged`，返回实际新增人数；无新增时不产生写入与信号。`addName`/`updateName`/`removeName` 等单条编辑语义不变 |
| 14.2 | `ClassWidgets/pages/settings/Extensions/RollCall.qml`（改动 12.5/13.1 的增量） | `importFromFile` 改为「`importNamesFromUrl` 解析一次 + `mergeNames` 合并一次」，替代原先逐名 `addName` 的循环；「导入完成：新增 x 人，跳过重名 y 人」提示文案与语义不变 |

**验证**：临时测试工程（仓库外）直连 `ConfigStore` + `RollCallService` 断言 22 项
全过——5000 人批量合入仅 1 次 `dataChanged`/`namesChanged`（18 ms，旧逐名路径为
5000 次）；存量重名/批内重名/空名/裸字符串/权重越界钳位、BOM/CRLF/空行/文件内
去重解析均符合契约；20000 行 TXT 解析 + 合入 70 ms。

## 改动 15：课表速览条加宽 + 小组件外观/隐藏联动（2026-10-01）

用户反馈：速览条字体过小、条过窄；圆角不跟随「小组件外观-圆角」设置；小组件
隐藏（滑出屏幕边缘 / 收成浮窗）后速览条仍有一截残留。本次只改**新增文件**
`SchedulePeekBar.qml`（改动 12.7 的增量），不触碰上游同步区任何文件。

| # | 文件 | 改动 |
|---|---|---|
| 15.1 | `ClassWidgets/Components/SchedulePeekBar.qml`（改动 12.7 的增量） | ① **加宽**：格径 26→32、缩写字号 14→18、格间距 6→8、内边距 8/5→10/6（条高 36→44），并与小组件「缩放」同键联动（`preferences.scale_factor` 同乘 `cellSize`/`rowSpacing`/`fontSize`/内边距）；② **外观跟随**：圆角 = `preferences.widget_corner_radius × scaleFactor`（按条高一半封顶，22px 圆角配 44px 条高即胶囊形）、背景不透明度 = `preferences.opacity`（均与 `Theme/components/Widget.qml:10,26` 同键同法，字体/字重原本已跟随）；③ **隐藏跟随**：`shouldShow` 增加最高优先级判据 `widgetsHidden = interactions.hide.state`（点击隐藏与自动隐藏任务共用该键，含浮窗模式），小组件隐藏时整条 `visible=false`，并经既有 `contentGeometryChanged → 蒙版重算`信号链同步摘出；④ 顺带修复 sep 格 `text: modelData.text` 对 QString 赋 undefined 的 QML 告警（`|| ""`）。 |

**验证**（本机独立沙箱 + `--smoke-test`，与运行中实例完全隔离的临时根目录）：
- 默认外观（scale=1 / opacity=1 / radius=22）：速览条 101×44，字号 18、格径 32、
  圆角 22（胶囊形）、不透明度 1；
- 外观联动（scale=1.5 / opacity=0.5 / radius=40）：151×66，字号 27、格径 48、
  圆角 33（40×1.5=60 按 66/2=33 封顶）、不透明度 0.5；
- `interactions.hide.state=true`：`visible=false`（修复前 top_center 锚点会残留
  条高的大部分）；置回 `false` 且 auto 条件满足（已下课 + 有下一节）时 `visible=true`；
- 冒烟加载零 QML 告警（退出 teardown 的 context 置空告警除外）。既有 C++ 直连
  `connect SIGNAL(widthChanged(qreal))` 因签名不符自始未生效的告警仍在：蒙版更新
  一直由 WidgetsContainer 的 `contentGeometryChanged` 中转链承担，功能无影响，
  本次未动 C++。

## 改动 16：托盘面板（TrayPanel）整体移除，托盘交互收敛到原生菜单（2026-10-01）

用户反馈：托盘右键已在 B4 改为 win32 原生菜单，但左键仍会弹出 TrayPanel
（快捷面板），"Qt6 Widgets 去除不够完全"。本次把左键面板整条链路拆掉：
托盘左键/中键/双击/右键统一弹原生菜单（`TrayIcon`），应用不再因托盘交互
创建任何 QML 窗口。

| # | 文件 | 改动 |
|---|---|---|
| 16.1 | `MainInterface.qml` | 删除 `trayPanelLoader`（Loader + TrayPanel 组件）与 `Connections.onTogglePanel` 分支；`import ClassWidgets.Windows` 随之不再被任何文件使用（该模块现无类型），一并移除 |
| 16.2 | `ClassWidgets/Windows/TrayPanel.qml` | **删除**（上游文件）。功能去向：课表切换 → 托盘菜单"Switch Schedule"（SwitchScheduleDialog，B4 已建）；宫格快捷方式 5 个目标中 4 个（设置/课程表/调休/换课）已在托盘菜单，插件广场本阶段为 no-op（改动 3）；What's New → 设置窗口关于页；调试器属 `app.debug_mode` 开发功能 |
| 16.3 | `ClassWidgets/Components/TrayShortcuts.qml` | **删除**（上游文件，仅被 TrayPanel 引用）。`UtilsBackend` 的 shortcuts 注册表/`executeShortcut` C++ 接口保留（`preferences.shortcuts` 配置与"调休"快捷方式信号路径仍在用） |
| 16.4 | `ClassWidgets/Components/qmldir` / `ClassWidgets/Windows/qmldir` | 移除 `TrayShortcuts` / `TrayPanel` 注册行（`ClassWidgets.Windows` 暂成空模块，保留 qmldir 供上游同步与后续类型回填） |

对应 C++ 侧（不属 QML 同步区，仅备忘）：`TrayIcon` 删除 `togglePanel(QPoint)`
信号（左键/中键/双击改发原生菜单），新增 `quitRequested`（托盘"退出"经
`AppCentral::quit()` 的 `exit(0)` 绕过 Qt 6.8+ quit() 的窗口关闭协商——悬浮
小组件窗口 onClosing 拒绝关闭会吞掉退出，此前表现为"要点两次退出"）；
`main.cpp` 移除 togglePanel 接线；`AppCentral` 删除 togglePanel 转发信号与
`onTrayTogglePanel` 槽。

## 改动 17：四插件移植到 Next 扩展（2026-10-02，four-plugins-to-extensions-plan.md A–F 全量实施）

四个上游 Python 插件以官方扩展（`extensions.*`，C++/QML 原生，不加载第三方代码）重实现。
C++ 实现全部在同步区外；本节登记**上游同步区改动 4 处**（17.1–17.4）与
**新增 QML 文件 3 个**（17.5–17.7；17.8 为既有文件增量登记）。禁止移植 Python 补丁注入范式与 `libs/` vendored 依赖。

**改动（上游同步区，4 处）**：

| # | 文件 | 位置 | 改动 |
|---|---|---|---|
| 17.1 | `widgets/Time.qml` | 组件根属性 + `text` 绑定 + `titleTimer` + 三处 `AnimatedDigits` | 新增 `extOn` 门控 + `timeTweaks` 只读绑定（`extensions.display_tweaks.*`，缺键或扩展关闭回退上游默认）——秒显隐、年月日星期分量过滤、并排/交替标题（交替间隔可配 500–30000ms、交替淡入淡出经 `OpacityAnimator`，注：基类已有 `Behavior on opacity`，此处不再声明同属性 Behavior）、`animEnabled` 透传；标题模式补充质检修正：`isAlternate = !extOn || 键值==="alternate"`——关扩展恢复上游"3s 交替"基线（原缺省按 side_by_side 与基线不符）；不整文件替换。同步上游：增量绑定，若上游重写时间组件把同名属性手工合入 |
| 17.2 | `widgets/eventCountdown.qml` | 根属性 + 三处 `AnimatedDigits` | 新增 `extOn` 门控 + `countdownAnim` 绑定（`display_tweaks.countdown_animation`），透传 `animEnabled`；其余不动。同步上游：单属性增量 |
| 17.3 | `ClassWidgets/Components/WidgetsContainer.qml` | `hideMargin`、`calcY`、`Connections` 区 | `hideMargin → hideDepthOverride>=0 ? override : 平台默认（macOS 48 / 其余 24，-1=跟随平台哨兵）`，顶部三停靠 `y → displayTop>=0 ? displayTop : offset_y`；扩展开关门控 `extOn`（范式同 SchedulePeekBar，关闭回平台默认）；新增特定课程不隐藏纠正（`ScheduleRuntime` 状态变化经 `Qt.callLater` 纠正 `hide.state/mini_mode`，经 `currentEntry` 取名，`DisplayTweaks.excludedSubjects` 解析，≤20）；300ms 轮询无（现为 `hideFade` Behavior + C++ 绑定直驱）。同步上游：覆盖绑定增量，锚点为 `hideMargin`/`calcY` |
| 17.4 | `ClassWidgets/Theme/components/AnimatedDigits.qml` | `animEnabled` 属性 + `onValueChanged` | 新增 `animEnabled` flag 门（关闭时直接切值、不启动 `progressAnimation`；`layer.enabled` 保持常开，见文件头 A3 纪律）。同步上游：单 flag 增量 |

**改动（项目自有主题文件，非上游同步区，登记备查）**：`SchedulePeekBar.qml`（改动 12.7 的增量）加 `displayMode peek|full` 全量条视图（全名 + 剩余倒计时横向 `ListView`、空态 `今天还没有课程~`（常驻模式当天无条目时展示）、gap 分割渐变竖条、`computeTargetX` 左 20% 定位（delegate 实际几何登记优先）+ 400ms 动画 + 拖拽手势期间暂停、结束后 4s 恢复 + QML 本地 1s 跟随 Timer）。

**新增（QML，4 个）**：

| # | 文件 | 说明 |
|---|---|---|
| 17.5 | `ClassWidgets/pages/settings/Extensions/DisplayTweaks.qml` | P1 显示增强配置页：组件动画/时间显示/标题布局/几何/特定课程不隐藏 5 组卡 + 健康自检黄条（只读 `DisplayTweaks.healthy`，自检由 C++ 主窗口 QML 就绪后驱动）；排除科目 ≤20 上限禁用加号、全半角逗号分隔、隐藏深度 -1=自动（SpinBox 特殊显示） |
| 17.6 | `ClassWidgets/Components/dialogs/AddOverlayMemberDialog.qml` | P1 堆叠 overlay 二期挂载点：标成员（`settings._overlayMember`）+ presets 摆放说明；独占行/就地编辑行由二期任务接入 `overlayMember` role。计划 §4 B 二期 |
| 17.7 | `ClassWidgets/Windows/LessonsBoard.qml` | P4 白板二期挂载点：单窗双主题（纯白/纯黑）全宽课程条 + 大字号倒计时 + 画笔工具栏挂载点；独立 `Frameless+Tool+StaysOnTop`，不进主窗口蒙版。计划 §5 E 二期 |
| 17.8 | `ClassWidgets/Windows/RollCallFloat.qml`、`Windows/RollCallResult.qml`、`pages/settings/Extensions/RollCall.qml`、`pages/settings/Extensions/Weather.qml`、`pages/settings/Extensions/SchedulePeek.qml` 的增量（非新增文件，记于此） | 点名：悬浮窗尺寸/悬浮-实心样式/`click_hide`/上课隐藏（1h 兜底）/灵动通知播报（播前暂显、播后还原由 C++ `RollCallService::announce` 全权负责，还原 Timer 不随窗销毁）；结果窗 60ms flicker + `animation_seconds` + 提前结束 + 金色 `#FFE08A` OutBack + 可拖动 + 右下 18px 缩放 + 几何持久化（恢复尺寸钳屏幕范围、逐键 isKeyLocked、副屏负坐标可还原）；session 全点完也开结果窗兜底；设置页加悬浮窗/结果通知/数据源三选一（SecRandom 条件桩）/试抽/权重清零/DOCX 导入。天气：NMC 源 + `auto_location` + `nmcCode`（只透传 NMC 字母站号，不回填数字 cityId）。速览：`display_mode` 选择。计划 §4 C/§5 D/§5 E 一期 |

配套改动（C++，同步区外，登记于此供追溯）：新增 `DisplayTweaksService.{h,cpp}`（健康自检 + 排除科目解析，`DisplayTweaks` 上下文）、`SecRandomBridge.{h,cpp}`（仅 Windows，注册表 + C–J 盘 + 版本判定 + 只读监听桩，二期接线）、`weather/providers/NmcProvider.{h,cpp}`（免 Key：站号索引 + 实况预报 + 预警 Top3 + 内部备源回退；`CityInfo.nmcCode`）；扩展 `RollCallService`（docx 经 `QZipReader`/多编码/去序号/行内分割/`#` 注释、`mergeNames` 上游权重迁移、`resetWeights`/`testDraw`/`announce` 含 hide 层播报还原）；`ExtensionManager::definitions() +1`（`classwidgets.ext.displayTweaks`）+ `migrateMoreSettingsConfig`（`migrated` 标记键幂等）；`ConfigStore`（`display_tweaks` 15 键 + `roll_call` 6 键 + `schedule_peek.display_mode/board_strokes` + `weather.auto_location`，枚举白名单与数值钳位）；`WidgetsModel`（`OverlayMemberRole` +1 role，不改既有 9 slot 签名 + `overlayEditingId/overlayListMode` + `_overlayLocked` 卸载保护）；`WeatherService`（注册 NMC + `autoLocate` IP 双源 + `auto_location` 启动消费）；`WidgetsWindow`（QML 就绪后以真实挂载点驱动健康自检）；`AppCentral`（`displayTweaks` 属性 + `DisplayTweaks` 上下文）；`AppWindowManager`（`LessonsBoard` 窗口）；`CMakeLists.txt` 注册 6 个新文件 + `Qt6::CorePrivate`（QZipReader）。

配套翻译：`app/assets/locales/*.ts` 8 语种经 `lupdate` 提取 77+5 条新字符串（`DisplayTweaks`/`SchedulePeekBar`/`LessonsBoard`/`AddOverlayMemberDialog`/`RollCall`/`RollCallResult`/`Weather`/`SchedulePeek`/`Extensions` 上下文；`zh_CN`/`zh_SIMPLIFIED`/`zh_HK` 全译，`en_US` 中文源给出英文译文，`ja_JP`/`it`/`ta` 新条暂空回退源文，`lzh` 因旧文件语言标签不被 `lupdate` 6.8 识别未自动更新、新条运行时回退源文），`.qm` 已用 `lrelease` 重新生成（无 error）。

**验证**（本机 Linux 条件验证，Qt 6.8；全量构建需 Qt 6.9 + MSVC，冒烟需 Windows，均未执行——见计划 §7，待有条件时按本文档已完成标记质检）：
- `lupdate` 全量提取无 error（含本次新增/修改的 13 个 QML；`SchedulePeekBar.qml` 初版嵌套三元曾使 `lupdate` 解析失败，已改写为 `fullSubText` 语句函数后通过）；
- `qmllint` 13 个触及文件零 error（仅隔离性 import/context 警告，与改前同类）；
- `g++ -fsyntax-only`（Qt 6.8 头）：全部新增/修改 C++ 通过，`AppCentral.cpp` 除外——其 `TrayIcon.h → QtCore/qt_windows.h` 为预存 Windows-only 依赖（改前即有），Linux 下不可验证，`AppCentral.h` 单独通过；
- `lrelease` 8 语种无 error。

## 改动 18：四插件移植质检修正（2026-10-02，改动 17 全量质检后修复）

5 路只读质检（框架层/P1 显示/P2 点名/P3 天气/P4 课程条）共产出 61 条发现，按"P1 全修 +
高价值 P2 修 + 文档失真全修"原则落地本节；二/三期挂载点（AddOverlayMemberDialog 调用方、
SecRandom 接线、LessonsBoard 挂载）按计划口径不在本期，仅修可运行性。全部 C++/QML 改动
经 MSVC 全量重建 + offscreen 冒烟验证。

**扩展开关与门控（P1）**：
- `Time.qml` / `eventCountdown.qml` / `WidgetsContainer.qml`：新增 `extOn` 绑定（范式同
  SchedulePeekBar 12.7：绑定内读 `Extensions.extensions` 建立通知依赖 + `isEnabled`），
  displayTweaks 关闭后整体回退上游默认（此前关闭扩展不产生任何行为）。
- `FloatingWidget.qml`：三处 `AnimatedDigits` 补 `animEnabled: countdownAnim` 透传（原浮窗
  数字不受动画开关控制）。

**显示增强（P1/P2/P3）**：
- `SchedulePeekBar.qml` full 模式：`isNext` 判定改用独立的 items 下标 `nextItemIndex`
  （原混用含分隔格的 cells 下标索引 items，TypeError 冻结整条绑定）；空态占位在常驻模式
  当天无条目时可达；4s 暂停窗口自手势结束起算且 tick 避让 moving/flicking；
  `computeTargetX` 优先用 delegate 登记的真实 contentX 并钳制首尾边界；5 类 activity
  标题过滤按 `fullNameOf`（title 为空时查科目名）。
- `DisplayTweaks.qml`：健康自检不再硬编码 `healthCheck(true, true)`（改由 C++ 主窗口
  QML 就绪后以真实 findChild 结果判定，黄条分支可达）；"添加当前课"达 20 门禁用 +
  科目名经 subjectId 回退；排除科目文本框全半角逗号均作分隔；隐藏深度 SpinBox 支持
  -1=自动（跟随平台默认，恢复 macOS 48 语义）。
- `ConfigStore.cpp`：`hide_depth` 钳位 `[0,200]` → `[-1,200]`，默认 24 → -1（哨兵）；
  `ExtensionManager::migrateMoreSettingsConfig` 幂等闸门改为专用 `migrated` 标记键
  （原"目标非默认痕迹"判定在旧插件配置全默认时每次启动重写目标键，回滚用户调参）。

**点名（P1/P2/P3）**：
- `RollCallService.cpp`：docx 解析整链路改用 `QZipReader`（`Qt6::CorePrivate`）——替换
  手写 EOCD/中央目录解析 + `__has_include(<zlib.h>)` 门控的 raw inflate（MSVC 下 Qt 不
  导出公共 zlib 头使 docx 静默失效；手写解析负偏移越界读；损坏流可致 GUI 死循环）；
  `<w:tab/>` 转制表符；UTF-16BE（FE FF）BOM 字节序交换修正 + 无 BOM UTF-16 双向探测。
- `RollCallService::announce` 收编 hide 层"播前暂显/播后还原"（还原 QTimer 挂服务对象 +
  aboutToQuit 退出兜底；原还原 Timer 在悬浮窗 QML 内，click_hide/播报期关窗路径下窗口
  先于触发销毁，hide 层永久展开且展开态可落盘）；`RollCallFloat.qml` 相应瘦身。
- `RollCallResult.qml`：结果窗几何持久化真正生效（原 onVisibleChanged 无条件重居中覆盖
  恢复值）；恢复尺寸钳屏幕范围而非 min() 到默认值（拖大可还原）；逐键 `isKeyLocked`
  （原误检 `button_x`）；副屏负 virtualX 坐标可还原（pointOnAnyScreen 判定）；flicker
  兜底分支二次随机修正；移除缩放期间"改宽即重居中"打架行为；session 全员点完也开结果窗
  （clearSession 入口可达，解除功能死锁）。
- `ConfigStore.cpp`：移除死键 `extensions.roll_call.mode`（全仓无读写方）。

**天气 NMC（P1/P2/P3）**：
- `Weather.qml` `selectCity`：`nmcCode` 只透传 NMC 字母站号（原 `|| city.cityId` 把数字
  码污染进 nmcCode，NMC 源永远请求错误站号）；`resolveStationCode` 对 nmcCode/cityId
  均做纯数字拒绝 + 同名县市按省消歧。
- `NmcProvider.cpp`：索引落盘改 `AppPaths::configsRoot()` + version 字段（原
  QDir::current 相对路径 + 开发树 hack 打包后失效）；移除指向不存在 example/ 目录的
  迁移死代码；`/rest/weather` 数值宽容字符串（原字符串返回值全被当 9999 哨兵丢弃）；
  快照有效性按实际解析结果判定（原恒 true 掩盖整轮失败）；findAlarm 取前 3 页（原 1 页）；
  白色预警 rank 低于蓝色（原钳成同档）；预警标题他省省级名交叉剔除；day-cache 跨天清理；
  info 映射表改确定性最长匹配（原 QHash 迭代序随机）。
- 备源回退（weather.com.cn）重写：toy1/search JSONP 前缀剥离 + d1 文本页
  `var dataSK/cityDZ` 正则解析，补温度/湿度/风/描述与今日最高最低温（原把文本响应交给
  JSON 解析器必报 parse 且丢弃已解析 alerts）。
- `WeatherService`：`weather.auto_location` 获得启动消费（start 时无城市且开关开 →
  autoLocate；原开关无任何运行期效果）；ip-api 分支城市 JSON 补 `adcode/wcnKey` 键。

**其他**：
- `WidgetsModel` 不动（AddOverlayMemberDialog 经 `ComboBox.find/currentText` 公共 API
  取值，替换 QML 不可调用的 `QAbstractItemModel::rowCount/index/data` 虚函数调用）。
- `widgets/weather.qml`（改动 5 自有新增文件的增量，记于此）：temperature/tempMax/
  tempMin 逐键判缺防 NaN°（补充质检修正——NMC 备源可能只补到最低温、主路径可能仅
  weatherCode 有效；新增"最低 %1°"文案已入 8 语种 .ts/.qm）。
- 头注释修正：`DisplayTweaksService.h`（不持有 ConfigStore）、`WeatherService.h`
  （windScale 契约含 NMC）、`NmcProvider.h`（协议描述与实现对齐）。

## 改动 19：随机点名两处交互修复（2026-10-02，用户实测反馈）

**悬念收口（播报不得先于揭晓）**：
- `RollCallFloat.qml`：`rollCall()` 移除即时 `RollCall.announce(names)` —— 原实现在点
  「点 N 名」的同一瞬间播报，灵动通知先于结果窗滚动动画弹出，动画未放完结果已泄底；
- `RollCallResult.qml`：新增 `announcePending` 收口，播报统一在滚动定格（`finishReveal`，
  含自然结束、卡片点按提前结束、「停止」按钮三条路径）后由结果窗执行一次。首次点名
  （先 draw 后开窗）与「再点 N 名」共用本收口。参考插件 rollcall-result.qml 亦在
  `finish()` 定格后才回调 `backend.onPicked` 播报。

**「再点 N 名」动画重播**：
- `RollCallResult.qml`：卡片根 `TapHandler`（点按提前结束 flicker）此前会与按钮的自绘
  `TapHandler` 同时收到同一次点按（Qt 指针处理器沿祖先链分发，子件不截断父件），
  点「再点 N 名」时 `draw→startReveal` 刚点亮的滚动被同一抬手立即掐灭；现按
  `eventPoint.position` 命中判定（`tapOnControl()`：按钮行 controlsRow / 缩放手柄
  resizeHandle 矩形范围）跳过控制件上的点按。
- 动画重播改由 `RollCall.drawCompleted` 信号驱动（原读 `drawn` 值变化）：single 模式
  连续抽中同一人时 `lastDraw` 值相等，值比较可能不触发 `onDrawnChanged`，"再点"看上去
  毫无反应；信号驱动保证每次 draw 必重播。

**验证**：`_analysis/rollcall_harness/` 新增 `qmltestrunner` 回归套件（offscreen，Qt 6.10.3；
测试副本由生产 QML 自动生成，仅替换上下文属性为 mock）。8 用例结果窗套件（悬念收口 +
再点重播 + 提前结束 + 同名单重抽）与端到端用户旅程（悬浮窗点击 → 结果窗 → 再点 → 关闭）
全过；同一套件对修复前副本运行失败 5+1 例（复现原缺陷），证明用例真实覆盖两个 bug。
改动文件 `qmllint` 无新增告警类型（仅既有 C++ 上下文属性 unqualified 模式）。

## 改动 20：当日作业扩展（2026-10-03，第 5 个官方扩展 `classwidgets.ext.homework`）

本仓库自有模型（无上游对应），计划全文与实施增补见
[`homework-extension-plan.md`](homework-extension-plan.md)。C++ 实现全部在同步区外；
本节登记**上游同步区改动 4 处**（20.1–20.4）与**新增 QML 文件 4 个**（20.5–20.8）。
作业浮窗是独立窗口，不进主窗口 `WidgetsContainer` 几何链路，也不并入
`WidgetsWindow` 的窗口 mask（点名悬浮窗/结果窗同理），因此本扩展**不含**
`WidgetsContainer.qml` / `WidgetsWindow.cpp` 类的容器与蒙版增量。

**改动（上游同步区，4 处）**：

| # | 文件 | 位置 | 改动 |
|---|---|---|---|
| 20.1 | `ClassWidgets/pages/editor/Subjects.qml` | `openEditDialog` 形参 + delegate 绑定 + 编辑对话框 `:159-171` / `:198-211` | 科目编辑对话框新增「需要布置作业」开关（`Switch` + `?` 说明 `Flyout`，照抄 `Held in homeroom` 行式样，`openEditDialog` 增 `needsHomework` 形参并回写 `ScheduleEditor.updateSubject` 尾参）；`subjectNeedsHomework.checked = needsHomework !== false`（缺省开）。同步上游：单开关增量 + 形参增尾参 |
| 20.2 | `ClassWidgets/Components/editor/SubjectClip.qml` | `editRequested` 信号 + 属性区 | 信号增 `bool needsHomework` 形参并透传；新增 `property bool subjectNeedsHomework: modelData.needsHomework !== false`。同步上游：单属性增量 |
| 20.3 | `src/qml/MainInterface.qml` | `Component.onCompleted` 前的子项区 | 新增常驻 `HomeworkTrigger{}`。触发逻辑必须挂主窗口（作业浮窗关闭即销毁，不能在浮窗内做触发）；扩展开关关闭时组件内所有路径早退。同步上游：新增单组件 |
| 20.4 | `ClassWidgets/Components/qmldir` | 模块注册区 | 注册 `HomeworkTrigger 1.0` 与 `dialogs/HomeworkEditDialog 1.0` 两行。同步上游：纯增量 |

**新增（QML，4 个）**：

| # | 文件 | 说明 |
|---|---|---|
| 20.5 | `ClassWidgets/Components/HomeworkTrigger.qml` | 下课自动显隐触发器（F1/F3）。常驻挂 `MainInterface`；状态源只读 `AppCentral.scheduleRuntime`，零自开秒级 `QTimer`（延迟与 600ms 通知均为单发业务定时器）。下课判定：`currentStatus` 从 `class` 切到 `break/free`（`activity`/`preparation` 不算下课沿、也不取消已排期延迟）；延迟 `delay_minutes ∈ [0,10]` 单发 `Timer`（触发沿快照 `pendingDelayMs`），期间切回上课/预备 → `stop()` 并收起浮窗；到期复核状态，漂移（调休/切课表/休眠唤醒）按取消。上课/预备**一律**收起浮窗、不区分自动/手动打开（否则启动补开、开关开启、设置页手动打开的窗口永不隐藏即「常驻显示」）。通知去重指纹「日期\|上节课id\|endTime」**只作用于通知**，浮窗触发不套指纹（否则拖堂后再次下课会吞掉浮窗） |
| 20.6 | `ClassWidgets/Windows/HomeworkFloat.qml` | 右侧作业浮窗（F2/F6/F7）。`Frameless\|StaysOnTop\|Tool`，首启右侧垂直居中，选屏同 `MainInterface`/`RollCallFloat`。**拖动收窄到标题栏而非整卡**——卡片主体是 `ListView`（`Flickable`），祖先级 `DragHandler` 会与滚动手势抢指针（整卡拖动会让列表滚不动）；右下 18px 缩放手柄（min 240x200，钳屏），`persistGeometry` 逐键 `isKeyLocked` 双检，多屏负坐标照 `RollCallResult`。`locked` 默认 `true` 禁拖动并隐藏手柄。内容：标题栏 + 当日作业 `ListView`（`Text.Wrap`，优先级整行字体变色 orange/blue/green）+ 底部 `+` 行；选中行浮现编辑/删除二键（编辑态隐藏缩放手柄，避开热区）。`onClosing` 拦截转 `WindowManager.closeHomeworkFloat()` |
| 20.7 | `ClassWidgets/Components/dialogs/HomeworkEditDialog.qml` | 编辑对话框（F5/F7）。科目下拉（首项「不指定」+ 过滤 `needsHomework === false`，**打开时刷新快照**避免编辑期间课表推送重建模型打断选择）、正文必填（Ok 门槛唯一驱动是 `onTextChanged`，回填同值不触发故 `openFor` 末尾显式 `syncOkEnabled`）、优先级四选一。入口统一 `openFor(itemId, subjectId, content, priority)`：`itemId` 空即新建，其余回填，科目已删/被过滤则回退「不指定」 |
| 20.8 | `ClassWidgets/pages/settings/Extensions/Homework.qml` | 配置页（F4），`SchedulePeek.qml` 三件套范式：受锁约束 / 用户操作回写 / 初始化读取。锁定、延迟 0–10、自动展示、灵动通知、保留 1/3/7 天 + 「打开作业浮窗」入口（**必须受总开关约束**，否则禁用扩展后仍可从本页打开并使用完整编辑功能）。全部 `enabled:!isKeyLocked` 并在 `refresh()` 内显式刷新（`isKeyLocked` 是 `Q_INVOKABLE`、无通知依赖，插件锁键不会自动重算绑定） |

配套改动（C++，同步区外，登记于此供追溯）：新增 `extensions/HomeworkService.{h,cpp}`
（按天文件读写 + `retention_days` 过期清理 + `UnionTimer::tick` 跨天检测 + 作业布置通知，
QML 上下文名 `Homework`；**读/写双路**：`readDayFile` 的 `Corrupt` 态不得静默覆写，
`loadDayForWrite` 先备份 `corrupt-*` 再按空处理、留底失败必须放弃本次写入；
`sanitizeItems` 字段级自愈补缺失 `id`，`displayOnly` 额外剔除空正文）；
`ExtensionManager::definitions() +1`（`classwidgets.ext.homework`）；
`ConfigStore`（`extensions.homework` 9 键白名单 + 默认树，`window_x/y` 为 `OptInt`
允许 null 即「从未拖动」哨兵，`delay_minutes` 钳 0–10）；
`ScheduleModel.{h,cpp}`（`subject.needsHomework` 默认 `true` + 存量归一化补齐）、
`ScheduleEditor.{h,cpp}`（`addSubject`/`updateSubject` 尾参 `needsHomework=true`）、
`ScheduleConverter.cpp`（`defaultSubjects` 与导入路径同步）；
`AppCentral.{h,cpp}`（`homework` 属性 + `Homework` 上下文 + 启动补开 +
`extensionToggled` 开关接线：开启时正值课间/放学则立即弹出，上课期间不强弹；
关闭即下线）；`AppWindowManager.{h,cpp}`（`WindowId::HomeworkFloat` +
`open/closeHomeworkFloat` + `windowQmlPath`/`windowName`/`notInitializedMessage`）；
`CMakeLists.txt` 注册 `HomeworkService.{h,cpp}`。

**验证**：MSVC 全量重建（`build/Release/ClassWidgetsNext.exe`）通过。

## 改动 21：语音播报扩展（2026-10-04，第 6 个官方扩展 `classwidgets.ext.tts`）

功能蓝本 cw2-tts（`example/cw2-tts-main/`）原生移植：Qt6 TextToSpeech 离线合成
（Windows 走 winrt/sapi 后端），引擎自带播放（无临时文件、无 QtMultimedia），
对齐 cw2-tts 全量（通知监听/silent 跳过/五类模板/变量/引擎切换 + auto 故障转移/
音量/试听）+ 新增按通知来源 provider 的朗读范围过滤。
C++ 实现全部在同步区外；本节登记**上游同步区改动 0 处**与**新增 QML 文件 1 个**
（按下文纪律：新增文件不与上游冲突，上游若出现同名路径需复核）。
扩展列表页（`Index.qml`，改动 12.3）全自动枚举注册表，本扩展零改动即现。

**新增（QML，1 个）**：

| # | 文件 | 说明 |
|---|---|---|
| 21.1 | `ClassWidgets/pages/settings/Extensions/Tts.qml` | 配置页（Homework.qml 三件套范式：受锁约束 / 用户操作回写 / 初始化读取 + `activeFocus` 守卫防 dataChanged 打断模板输入）。启用开关 + 健康黄条（桩构建/无引擎时只读提示，范式照 DisplayTweaks.qml）+ 引擎下拉（auto 居首）/语音下拉 + 刷新/音量 Slider（0–100%，pressed 期间写回，落盘靠自动保存）/测试朗读 + 停止/五类模板 TextField + 重置 + 试听（示例值替换，照 cw2-tts settings.qml）/朗读范围 Repeater（`notificationProviders()` 动态列出，含作业/点名等后来注册项）。**必须走 `AppCentral.tts`，禁裸写 `Tts.*`**（文件名隐式类型遮蔽上下文属性，同 RollCall.qml 教训，见页头注释） |

配套改动（C++，同步区外，登记于此供追溯）：新增 `extensions/TtsService.{h,cpp}`
（QML 上下文名 `Tts` + `AppCentral.tts` 属性；`notified` 只读订阅，不过
`playNotificationSound` 路径；provider 后缀映射 `.class/.activity/.break/.free/`
`.preparation` → 模板，残留 `{…}` 占位回退 `title。message`，标点清理对齐
announcer.py；运行时上下文经 `ScheduleRuntime` 属性动态读取，
`currentSubject`（name/teacher/location）→ `currentEntry.title` 兜底 →
`nextEntries[0]` 经 `subjects` 解析 `next_*`；FIFO 朗读队列串行语义；
auto 下 `errorOccurred` 换下一后端并重试当前句，非 auto 放弃；
`provider_enabled` 整表读-改-写——provider_id 含点，不能走点分路径直写）；
`ExtensionManager::definitions() +1`（`classwidgets.ext.tts`）；
`ConfigStore`（`extensions.tts.engine/voice/volume` 进 `kScalarSpecs`——engine 用
`Str` 而非枚举白名单：后端名平台动态枚举，白名单会把真实后端值打回 `auto`；
`templates`/`provider_enabled` 进默认树 + `normalizeTtsMaps` 形状修正，
`volume` 钳 0–1）；
`AppCentral.{h,cpp}`（`tts` 属性 + `Tts` 上下文 + 启动期已启用补接线 +
`extensionToggled` 接线：开→恢复订阅，关→断开并 `stopSpeaking()`）；
`CMakeLists.txt`（注册 `TtsService.{h,cpp}` + `find_package(Qt6 COMPONENTS
TextToSpeech QUIET)`：存在即链真实后端，否则全工程注入 `CWN_NO_TTS`
切桩实现——本机/CI 的 Qt 均未装该组件，当前构建即走桩路径，
装模块后重配 CMake 自动切回真实后端）。

配套翻译：本次未补 `.ts` 词条（随翻译流程滞后，见 tts 计划 §4.7；`Extensions`
上下文"语音播报"等名称/描述未译语种回退中文源文，与既有扩展同策略）。

**验证**（2026-10-04，本机 Qt 6.10.3 + MSVC + offscreen）：
- `cmake` 重配置确认 `Qt6 TextToSpeech NOT found: TTS builds as stub (CWN_NO_TTS)`
 （本机/CI 均未装该组件，走桩路径；装模块后重配自动切真实后端）；
- MSVC Release 全量重建 exit 0（`build/Release/ClassWidgetsNext.exe`），零警告；
- 冒烟 `--smoke-test`（隔离沙箱 `CW2_APP_ROOT` + offscreen）：扩展关闭/预置启用
  `classwidgets.ext.tts` 两种状态均 `Event loop finished with code 0`，
  `configs.json` 正确落盘 `extensions.tts`（engine auto/voice ""/volume 1.0/
  五类默认模板/provider_enabled {}），`extensions.enabled` 开关往返保持；
  残留 WARN 均为退出 teardown 上下文置空告警（与改前同类）；
- `qmllint`：Tts.qml 零 error（告警类与 Homework/RollCall 基线一致）；
- QML 真机 harness（`%TEMP%/cwn-tts-qmltest/`：C++ 无 moc 加载器 + mock 上下文 +
  真实 RinUI，offscreen）7/7 通过：页面 Ready、refresh() 回填 5 模板 + 同步
  3 provider 开关、`previewText` 变量替换无残留、`Configs.dataChanged` 触发重读。
  该 harness 抓到一处生产 bug 并已修复：`notificationProviders` 为函数，
  QML 漏 `()` 取到函数引用致朗读范围 Repeater 无数据——现命令式设 model +
  `providersChanged` 转发（`NotificationService::notificationProvidersChanged`）重建
  （首版做法；质检后模型改为绑定式属性，见下方补遗）：
- 真实后端编译风险（本机无模块）：对照 Qt 官方成员表逐项核对所用 API
  （`availableEngines` static/`availableVoices`/`engine`/`setVoice`/`setVolume`/
  `say`/`stop()`/`stateChanged(State)`/`errorOccurred(ErrorReason,QString)`/
  构造器），据此把错误枚举从 `Error` 修正为 `ErrorReason`（6.8+；
  `stop()` 无参调用在 6.10/6.12 均合法——后者带默认 `BoundaryHint` 实参）。

### 质检修复（2026-10-04）

上述为改动 21 的首版实施记录。首轮全量质检另发现 6 类问题，已在本仓库修完，
不改变本节的登记口径（C++ 改动均在同步区外，QML 改动均落在新增的 `Tts.qml`）：

- **朗读队列有界**（`TtsService`）：队列上限 8 条（溢出丢最旧并记 debug）、入队后
  60s 仍未播到即作废（排他式 FIFO 下用户早已离开该课）、单条文本截断 300 字——
  三者共同封住「开关扩展后积压一批通知、上课后集中朗读」的最坏路径。
- **模板单遍替换 + 悬空收尾**（`applyTemplate`）：改单遍 `QRegularExpression`
  扫描替换（原 8 次串行 `replace` 会把「替换值里的 `{…}`」当占位做二次替换，
  课程名含花括号即踩，且残留判据同样误报），未知变量名原样保留并置 `ok=false`
  整句回退；`next_subject` 为空时先删掉「下节课是」悬空短语与前置标点，
  再由既有 `stripPunct` 收尾（对齐 `announcer.py`）。
- **故障转移三处加固**（`onSpeechError`）：旧合成器改 `stop` + `disconnect` +
  `deleteLater`（原栈内同步 `delete` 会踩正在派发的 `errorOccurred` 信号栈）；
  新实例构造完成后以 `QTimer::singleShot(0, …)` 延迟重试当前句（构造栈内直接
  `say` 不安全）；候选后端按 `failoverPriority()` 定优先级 winrt(0) → sapi(1) →
  其他(2)，同优先级保持 `QTextToSpeech::availableEngines()` 的返回顺序。
- **换引擎清语音**（`setEngine`）：跨引擎语音 ID 不通用，换引擎时一并把
  `extensions.tts.voice` 清空并补发 `voiceChanged()`——否则新引擎按旧名匹配不到、
  设置页却显示「选了语音」；与 engine 同一批写入（`m_applying` 抑制 `dataChanged`
  回授），结尾只落盘一次。对齐 cw2-tts。
- **`providers` 绑定式属性**（`TtsService` + `Tts.qml`）：新增带
  `providersChanged` 的 `Q_PROPERTY providers`（转发
  `NotificationService::notificationProvidersChanged`），设置页 Repeater 改为
  `model: AppCentral.tts ? AppCentral.tts.providers : []`；原先在 `refresh()` 里
  命令式赋 model，会被拖动音量滑杆触发的 `dataChanged` 带着整份列表重建一次
  `SettingCard`（高频操作下的纯浪费）。`notificationProviders()` 命令式入口保留。
- **QML 三处修正**（`Tts.qml`）：①「停止」按钮 `enabled` 绑定
  `AppCentral.tts.speaking`（并接 `onSpeakingChanged`）——原实现任何时候都可点，
  空闲时点击无意义；② 语音下拉新增语言（locale）筛选下拉，`localeOptions` 由
  `voiceList` 的 locale 去重派生、首项「全部语言」，`voiceOptions` 按其过滤——
  Windows 上 winrt 一次可枚举数百个语音，不筛根本挑不出来；③ 删除
  `testField.enabled = !isKeyLocked("extensions.tts.volume")`——测试框是页内临时
  输入（`testSpeak` 不落盘），无对应配置键，绑音量锁键只会让锁 volume 的用户
  连试读都不能用。附带：`previewText` 补空模板守卫（空模板 = 该类不播报）、
  `{title}` 示例值改固定「通知标题」而非模板键显示名（对齐 cw2-tts）；五处
  「重置」删掉冗余 `root.refresh()`（`templatesChanged` 已覆盖，模板已是默认值时
  `setTemplate` 短路不发信号）；朗读范围说明补「需该来源的应用内通知保持开启，
  否则不会播报」。

## 修改申请流程
1. 尽量不动 QML：能由 C++ 宿主、部署脚本或 vendored 副本解决的，不改上游文件；
2. 确需修改 `app/src/qml/**` 时：在本文件追加条目（文件、原因、与上游的 diff 要点）；
3. 同步上游时：仅对本文档列出的改动点做三方合并。
