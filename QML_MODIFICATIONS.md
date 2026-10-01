# QML 修改记录（上游同步区纪律）

> 移植方案 §9.5：QML 目录是**上游同步区**——能不改就不改，改动必须集中记录在本文档，
> 并定期（建议每月）从上游 `main` 拉取 `src/qml/` 变更做回归。

## 当前状态：`app/src/qml` 共 13 处改动（4 处 M3 插件入口遮蔽 + 2 处 M5 方法名改写 + 2 处 Interactions 页 sourceSize 移除 + 1 处 WidgetLoader 失败恢复 + 2 个「即将上课」组件文件 + 1 处 AddWidgetsDialog 数量上限 + 2 处扩展功能接入（Settings 导航项 / WidgetsContainer 速览条挂载，改动 12），见下）
> 另有 11 个**新增文件**（非上游改动）：天气小组件及其设置页（改动 5）、
> 倒数日小组件及其设置页（改动 9）、扩展功能 7 个 QML（改动 12）。
> `app/src/themes/**` 不再逐字同步上游：改动 7（material Color.qml，A6 引入、本版修正）
> 与改动 8（WidgetLoader.qml 失败恢复，属 `app/src/qml`）已偏离上游。

| 目录 | 内容 | 与上游的差异 |
|---|---|---|
| `app/src/qml/` | 上游 `src/qml/` 全量（137 个 QML、8 个 qmldir，HEAD `1e66f09`） | **多处（见改动 3、4、6、8、9、10、11、12）** |
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

## 修改申请流程

1. 尽量不动 QML：能由 C++ 宿主、部署脚本或 vendored 副本解决的，不改上游文件；
2. 确需修改 `app/src/qml/**` 时：在本文件追加条目（文件、原因、与上游的 diff 要点）；
3. 同步上游时：仅对本文档列出的改动点做三方合并。
