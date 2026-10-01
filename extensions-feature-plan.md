# 「扩展功能」模块实施计划（已实施）

> 状态：**已实施**（2026-10-01 完成全部 27 项任务与收尾，构建与冒烟测试通过，实施记录与偏差点见 §13）。
> 本文档按「阶段 × 难度 × 执行顺序」组织，保留计划全文供追溯与验收比对。
> 评审日期：2026-10-01。产品决策经用户确认，见 §10。
> 参考先例：`weather-multi-provider-plan.md`（天气多数据源，已实施）。

---

## 1. 背景与定位

Class Widgets Next 现状：内置小组件由 `src/core/BuiltinWidgets.cpp:37-128` 注册（8 个，含天气）；设置窗口完整可用（`app/src/qml/ClassWidgets/Windows/Settings.qml` 的 `navigationItems` + `subItems` 二级导航）；课表运行时 `src/core/schedule/ScheduleRuntime.h` 已完整暴露当天条目/当前节/下一节等属性；插件系统为 Phase 2 空壳（`PluginManagerStub`）。

新增**「扩展功能」**产品概念：

| 概念 | 制作者 | 现状 |
|---|---|---|
| 小组件（Widget） | 官方 | 已有，小组件栏的基本单元 |
| **扩展功能（Extension）** | **官方** | **本文档新增**：与小组件不直接绑定、独立界面手动开关与调整的功能模块 |
| 插件（Plugin） | 社区 | Phase 2，本期不动，配置键 `plugins.*` 不复用 |

初期三项扩展：

1. **天气**（迁移）——天气小组件移入扩展功能，不再以自带组件出现，可手动开关，在「扩展功能-天气」界面配置；
2. **随机点名**（新增）——屏幕右上角可拖动点名按钮，展开点 1/2/3 名面板，居中结果窗口，txt 导入名单、增删改与权重（-100%~+100%）；
3. **课表速览**（新增）——小组件下方弹出/常驻的当日课表缩写条（条宽与小组件行对齐），当前课橙色圆形高亮；下一课仅课间/活动等非上课时段绿色圆形高亮，上课期间保持普通样式。

## 2. 总体架构

三层结构，各扩展在第三层互不依赖：

```
① C++ 扩展框架（同步区外，零登记负担）
   src/core/extensions/ExtensionManager.{h,cpp}
   ├─ 扩展注册表（id / 名称 / 图标 / 描述 / 配置页）
   ├─ 开关状态 ⇄ configs.json (extensions.enabled)
   └─ QML API：extensions 属性 / isEnabled / setEnabled / 信号
        │
② 扩展配置界面（新增 QML 文件，登记为「新增」）
   pages/settings/Extensions/  Index + Weather + RollCall + SchedulePeek
   └─ Settings.qml navigationItems 加一项「扩展功能」（上游改动，须登记）
        │
③ 各扩展的功能实现
   天气   → 既有 WeatherService + 天气小组件（受开关控制，配置收归扩展页）
   点名   → RollCallService + 独立悬浮窗/结果窗（绕开主窗口蒙版机制）
   速览   → SchedulePeekBar + WidgetsContainer 内嵌 + 蒙版扩展
```

架构要点：

- **扩展与插件严格分离**：不复用 `plugins.*` 键与 `Plugins.qml` 页面；`PluginManagerStub` 保持现状。
- **点名走独立窗口**：主窗口 `MainInterface.qml` 是全屏透明窗口，原生蒙版（`WidgetsWindow.cpp:216-284`）只包含 `widgetsFlow` 子项与 `floatingWidgetContainer`，其余元素不可见不可点；悬浮按钮与结果窗口若放进主窗口需改蒙版 + 上游 QML，故独立成窗。
- **速览走容器内嵌**：需要"显示在小组件下方"并随 hide/anchor 移动，放 `WidgetsContainer.qml` 根 `Column` 内最自然（`calcY()` 的 `height` 是 Column 总高，自动跟随），代价是必须同步扩展蒙版（C++，成本可控）。

## 3. 执行总表（顺序 × 难度 × 规模）

难度定义：**低** = 照既有范式平移；**中** = 有新逻辑但模式清晰；**高** = 跨层改动或新交互机制。
规模：**S**（≤1 文件百行级）/ **M**（1-2 文件数百行）/ **L**（3+ 文件或含新窗口交互）。

依赖关系：全部任务依赖阶段 A；B/C/D 三阶段彼此独立、可换序；E 收尾。

| 序 | 任务 | 阶段 | 难度 | 规模 | 依赖 | 一句话说明 |
|---|---|---|---|---|---|---|
| 1 | A1 ExtensionManager | A 框架 | 中 | M | — | 扩展注册表 + 开关状态 + QML API |
| 2 | A2 配置键落地 | A 框架 | 低 | S | A1 | `extensions.*` 进 defaultConfig/kScalarSpecs |
| 3 | A3 设置界面骨架 | A 框架 | 低 | M | A2 | Index 页 + 3 子页占位 |
| 4 | A4 导航接入 + 登记 | A 框架 | 低 | S | A3 | Settings.qml 加「扩展功能」项 |
| 5 | A5 框架验收 | A 框架 | 低 | S | A1-4 | 开关可切换、重启保持 |
| 6 | B1 天气定义改造 | B 天气 | 低 | S | A5 | maxInstances=1、列表过滤、settings 退役 |
| 7 | B2 开关接线 + 实例自动增删 | B 天气 | 中 | M | B1 | 开=自动添加、关=移除 |
| 8 | B3 配置迁移 | B 天气 | 中 | M | B2 | settings.city → weather.city、老配置迁移 |
| 9 | B4 WeatherService 生命周期 | B 天气 | 中 | S | B2 | start/stop 随扩展开关 |
| 10 | B5 扩展页 Weather.qml | B 天气 | 低 | M | B3 | 由 widgets/settings/weather.qml 迁移改造 |
| 11 | B6 天气验收 | B 天气 | 低 | S | B1-5 | 开关/迁移/全局城市生效 |
| 12 | C1 RollCallService | C 点名 | 中 | M | A5 | 名单存储 + 加权抽取 + txt 解析 |
| 13 | C2 设置页 RollCall.qml | C 点名 | 中 | M | C1 | 导入/增删改/权重滑杆/重复策略 |
| 14 | C3 悬浮按钮 + 展开面板 | C 点名 | **高** | L | C1 | 拖拽持久化 + 方向展开 + 面板交互 |
| 15 | C4 结果窗口 | C 点名 | 中 | M | C1 | 居中定位 + 再点 + 会话清空 |
| 16 | C5 窗口管理接入 + 开关接线 | C 点名 | 中 | S | C3,C4 | AppWindowManager 两窗口 + 启停 |
| 17 | C6 点名验收 | C 点名 | 低 | S | C1-5 | 见 §8.3 |
| 18 | D1 SchedulePeekBar 显示条 | D 速览 | 中 | M | A5 | 缩写 + 分组竖线 + 圆形高亮 |
| 19 | D2 显隐状态机 | D 速览 | 中 | S | D1 | auto/always + 弹出收起动画 |
| 20 | D3 容器插入 + 几何联动 | D 速览 | 中 | S | D1 | WidgetsContainer 插入（上游改动） |
| 21 | D4 蒙版扩展 | D 速览 | 中 | S | D3 | WidgetsWindow updateMask 并入速览条 |
| 22 | D5 设置页 SchedulePeek.qml | D 速览 | 低 | S | D1 | 模式 + 阈值 |
| 23 | D6 速览验收 | D 速览 | 低 | S | D1-5 | 见 §8.4 |
| 24 | E1 i18n | E 收尾 | 低 | M | B6,C6,D6 | 8 语种 .ts + lrelease |
| 25 | E2 QML_MODIFICATIONS.md 完整登记 | E 收尾 | 低 | S | 全部 | 改动/新增逐条登记 |
| 26 | E3 README/文档更新 | E 收尾 | 低 | S | 全部 | 功能描述 + 与插件区别 |
| 27 | E4 全量回归 | E 收尾 | 中 | S | 全部 | 冒烟 + 手动清单 |

**推荐执行顺序**：A → B → C → D → E。理由：A 是全部前置；B 最小且能验证框架开关接线；C 体量最大但完全增量、不碰既有窗口行为，回归风险低；D 涉及核心窗口蒙版与主界面几何链路（回归风险最高的改动集中在一处），放在框架与流程都验证之后处理。

## 4. 阶段 A：扩展功能框架

### A1 ExtensionManager（难度 中 · 规模 M）

新文件 `src/core/extensions/ExtensionManager.{h,cpp}`：

- `ExtensionDefinition`：`id / name / icon / description / settingsPageQml（可空）`。
- 静态注册表 3 项：

  | id | 名称 | 配置页 |
  |---|---|---|
  | `classwidgets.ext.weather` | 天气 | `pages/settings/Extensions/Weather.qml` |
  | `classwidgets.ext.rollCall` | 随机点名 | `pages/settings/Extensions/RollCall.qml` |
  | `classwidgets.ext.schedulePeek` | 课表速览 | `pages/settings/Extensions/SchedulePeek.qml` |

- QML 面：
  - `Q_PROPERTY(QVariantList extensions)` —— 列表页数据源：`{id, name, icon, description, enabled, hasSettings, settingsPage}`;
  - `Q_INVOKABLE bool isEnabled(QString id)` / `Q_INVOKABLE void setEnabled(QString id, bool enabled)`;
  - 信号 `extensionsChanged()`、`extensionToggled(QString id, bool enabled)`。
- 装配：`AppCentral::initialize()` 在 `configs->load()` 之后创建；`AppCentral::setupQmlContext()`（`AppCentral.cpp:253`）注册上下文名 `Extensions`（新增名字，与既有 13 个不冲突）。
- `CMakeLists.txt`：新源文件加入构建目标（QML 无需改，`install(DIRECTORY app/)` 自动随包）。

### A2 配置键落地（难度 低 · 规模 S）

- `ConfigStore::defaultConfig()`（`ConfigStore.cpp:491` 起）新增 `extensions` 分区（默认值见 §6）。
- `kScalarSpecs`（`ConfigStore.cpp:53-117`）登记各标量类型；`extensions.enabled` 数组照 `plugins.enabled` 先例（103-105 / 571-578 / 671-677 行）做结构校验。

### A3 设置界面骨架（难度 低 · 规模 M）

新目录 `pages/settings/Extensions/`，4 个文件（**新增文件**，按 QML_MODIFICATIONS.md「改动 5/9」先例登记）：

- `Index.qml`：扩展卡片列表（图标/名称/描述/Switch 开关 → `Extensions.setEnabled`），有配置页的卡片带「设置」入口 → `navigationView.push(...)` 进子页（跳页写法照 `pages/settings/Home.qml:14-16`）。
- `Weather.qml` / `RollCall.qml` / `SchedulePeek.qml`：本阶段先占位（FluentPage + 标题），内容随 B5/C2/D5 填充。
- 页面结构照 `pages/settings/notificationAndTime/Time.qml` 范式：`FluentPage` + 分组标题 + `SettingCard`；控件写回三件套 `enabled: !Configs.isKeyLocked(...)` / `onValueChanged: Configs.set(...)` / `Component.onCompleted: value = Configs.data...`。

### A4 导航接入 + 登记（难度 低 · 规模 S）

- `Windows/Settings.qml:30-96` `navigationItems` 在「个性化」后插入一项「扩展功能」（icon 取既有 Fluent 图标集，如 `ic_fluent_puzzle_cube_20_regular`；以实际可用图标为准）→ `pages/settings/Extensions/Index.qml`。**上游文件改动**，同时登记 QML_MODIFICATIONS.md（见 §7）。
- 可选：`pages/settings/Home.qml` 首页快捷卡片（若加，同样属上游改动并登记）。

### A5 框架验收（难度 低 · 规模 S）

设置窗口出现「扩展功能」页；三项扩展可开关；`extensions.enabled` 落盘；重启后状态保持；`isKeyLocked` 拦截正常。

## 5. 阶段 B：天气迁移（已确认：开启自动添加/移除）

### B1 天气定义改造（难度 低 · 规模 S）

- `BuiltinWidgets.cpp:97-104`：天气 `WidgetDefinition` 保留（widget_id `classwidgets.weather` 契约不变），设 `maxInstances = 1`；`settingsQml` 置空（右键设置入口取消，配置全部移入扩展页）。
- `WidgetsModel::definitionsList`（`WidgetsModel.cpp:189`）过滤：扩展未启用时天气定义不进「添加小组件」列表（数据源 `AddWidgetsDialog.qml:82`）；启用时可见（用户手动删掉实例后可自行加回）。
- 注：实例渲染层面，未启用即无实例（B2 保证），无需在 9 role/9 slot 契约上加过滤逻辑。

### B2 开关接线 + 实例自动增删（难度 中 · 规模 M）

- 开启 `classwidgets.ext.weather` → 向**当前预设**自动添加一个天气实例（已存在则不动）。
- 关闭 → 从**全部预设**移除天气实例（功能整体下线；因城市已收敛为全局配置，实例无独立数据，移除无损失）。
- 接线点：`ExtensionManager::extensionToggled` → `WidgetsModel.addInstance / removeInstance`（`WidgetsModel.cpp:268/309`）。
- 注意 `widgets_presets` 多预设语义：增删经 WidgetsModel 既有槽走 `syncCurrentPreset + saveConfig` 通路，不绕过模型直写配置。

### B3 配置迁移（难度 中 · 规模 M）

- 城市从每实例 `settings.city` 收敛为全局 `weather.city`（JSON 字符串，形状不变：`{cityId,name,lat,lon,province,adcode,wcnKey}`）。
- 迁移（ExtensionManager 初始化时一次性执行）：
  1. 扫描 `preferences.widgets_presets`，任一预设含 `classwidgets.weather` 实例 → 自动把 `classwidgets.ext.weather` 加入 `extensions.enabled`（老用户天气不丢失）；
  2. 首个非空实例 `settings.city` → 写入 `weather.city`（原实例键保留不删，无害）。
- `weather.provider / poll_interval / keys.*` 等既有键**全部不动**（沿用 `weather-multi-provider-plan.md` 的键位约定）。

### B4 WeatherService 生命周期（难度 中 · 规模 S）

- `AppCentral.cpp:58/66`：`WeatherService` 保持常驻创建（`AppCentral.h:42` 的 `weather` 属性非空，QML 无需判空改造），但 `start()`（60s 轮询）受扩展开关控制。
- `extensionToggled` 时 start / 停止轮询；关闭期间 `weatherData()` 返回契约中的 `unconfigured/stale` 状态即可，组件反正已被移除。

### B5 扩展页 Weather.qml（难度 低 · 规模 M）

- 由 `widgets/settings/weather.qml` 改造迁入（该文件为项目自有文件，非上游同步区）：城市搜索/选择 + 数据源 + 凭据 + 刷新间隔四张卡内容复用；城市读写改为 `weather.city` 全局键。
- `widgets/weather.qml:44` 改读全局 `weather.city`（替代 `settings.city`）；`widgets/settings/weather.qml` 退役（可保留文件待清理，或删除并在登记条目注明）。

### B6 天气验收（难度 低 · 规模 S）

- 开关切换 → 小组件自动出现/消失，「添加小组件」列表同步；
- 扩展页配置城市 → 组件刷新显示；数据源/凭据/刷新间隔行为与原一致；
- 老配置（含天气实例的预设）升级后自动启用、城市迁移正确、无损。

## 6. 阶段 C：随机点名（已确认：重复策略设置项可切换）

### C1 RollCallService（难度 中 · 规模 M）

新文件 `src/core/extensions/RollCallService.{h,cpp}`，经 `AppCentral.rollCall` Q_PROPERTY 暴露（照 `AppCentral.weather` 先例）：

- **名单存储**：配置键 `extensions.roll_call.names`，数组元素 `{name, weight}`，weight 为 -100~100 整数。
- **抽取算法**：有效权重 = `1 + weight/100` ∈ [0, 2]（**默认假设，见 §10.5**）：-100% → 0 永不被抽中；0% → 与他人等权；+100% → 概率加倍。单次「点 N 名」内**不重复**（不放回加权抽取）。
- **重复策略**：`extensions.roll_call.avoid_repeat` = `"single"`（每次点名从全体重新抽取）| `"session"`（会话内已点过的人排除，结果窗口关闭时清空）。
- `Q_INVOKABLE QVariantList importNamesFromUrl(QUrl)`：读 UTF-8 txt（每行一个名字，跳过空行、去重、剥 BOM），返回名单供设置页合并（新名字权重默认 0）。QML 无法读本地文件，导入必须走 C++。
- `Q_INVOKABLE QVariantList draw(int count)` + `lastDraw` 属性/信号：供悬浮面板与结果窗口取用。

### C2 设置页 RollCall.qml（难度 中 · 规模 M）

- 「从 TXT 导入」按钮 + `QtQuick.Dialogs` FileDialog，另支持手动添加；
- 名字列表：每行 = 名字文本框（可改）+ 权重滑杆（-100%~+100%，步进 5%，显示百分比）+ 删除按钮；重名校验；
- 重复策略选择（ComboBox：单次内不重复 / 会话内不重复）。

### C3 悬浮按钮 + 展开面板（难度 **高** · 规模 L）

新文件 `ClassWidgets/Windows/RollCallFloat.qml`，独立窗口（绕开主窗口蒙版机制）：

- `Window{ flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool; color: transparent }`，圆形「点名」按钮；
- 拖拽照抄 `FloatingWidgetContainer.qml` 组合拳：64-108 行钳位/恢复/持久化 + 290-370 行 DragHandler + 372-385 行 TapHandler 区分点击与拖动；位置存 `extensions.roll_call.button_x/y`（-1 = 未拖过，默认落在 `preferences.display` 选中屏幕右上角；多屏选择照 `MainInterface.qml:27-34` 遍历 `Qt.application.screens`）；
- 点击按钮 → 展开小面板（点 1 名 / 点 2 名 / 点 3 名 / 取消）：按钮中心在屏幕右半边则**向左**展开、左半边则**向右**展开（窗口宽度增长、按钮原位不动）；「取消」仅收起面板；
- 点「点 N 名」→ `RollCall.draw(N)` → 打开结果窗口。
- 屏幕分辨率变化时重新钳位（照 `reconcilePosition` 先例）。

### C4 结果窗口（难度 中 · 规模 M）

新文件 `ClassWidgets/Windows/RollCallResult.qml`：

- 独立窗口，屏幕中央（QML 内 `x/y = (screen - size)/2`，跟随 `preferences.display` 选屏；C++ 侧无现成定位工具，定位放 QML）；
- 显示抽中名字（大字号列表），下方「再点 1 名 / 再点 2 名 / 再点 3 名」+「关闭」；再点即重新 draw 并刷新；
- `session` 模式下会话排除名单持续生效；关闭结果窗口时清空会话排除名单。

### C5 窗口管理接入 + 开关接线（难度 中 · 规模 S）

- `AppWindowManager.h:84` `WindowId` 加 `RollCallFloat / RollCallResult`；三个 switch 映射（`windowName / windowQmlPath / notInitializedMessage`，`AppWindowManager.cpp:359-433`）各加 case；加 `Q_INVOKABLE openRollCallFloat/closeRollCallFloat/openRollCallResult/closeRollCallResult`。
- 扩展开启/关闭时由 `extensionToggled` 接线开关悬浮窗；启动时若已启用则自动打开。
- 窗口 `onClosing` 拦截 + 转 close 槽（照 `Settings.qml:17-20` 先例），销毁走 `releaseWindow` 既有 0ms singleShot 语义。

### C6 点名验收（难度 低 · 规模 S）

见 §8.3。

## 7. 阶段 D：课表速览（已确认：按间隔分组、下课弹出上课收起）

### D1 SchedulePeekBar 显示条（难度 中 · 规模 M）

新文件 `ClassWidgets/Components/SchedulePeekBar.qml`（与 `WidgetsContainer.qml` 同目录，同类引用无需 qmldir）：

- **数据映射**（全部现成，零新增 C++ 接口）：`AppCentral.scheduleRuntime.currentDayEntries`（含 break/activity、已应用 override）过滤 `type=="class"` 按时间排序；每节缩写规则：`simplifiedName` 非空取其首字、未设置取课程全名首字（经 `subjects` 按 `subjectId` 查找；参照 `widgets/upcomingActivities.qml:73-85` 缩写先例）。
- **分组竖线**：相邻两节课的间隔（前一节 end 到后一节 start 的实际时长，含其间课间/活动条目）≥ 阈值 → 插 `|`；阈值键 `extensions.schedule_peek.split_gap_minutes`（默认 15，设置页 5~60 可调）。普通 10 分钟课间不插。
- **高亮**：当前进行中的课（`currentEntry.id` 命中）→ **橙色圆形背景**；下一节即将开始的课（`currentDayEntries` 中第一个 start > now 的 class 条目）→ **绿色圆形背景**，但仅在课间/活动（`currentStatus !== "class"`）时点亮——上课期间下一节保持普通样式。每格单字，圆形背景叠于字下。
- 渲染：Row 单行（如「语 地 生 数 | 化 | 英 物 历 政 | 语 化」），格间距均匀，竖线为细间隔；条宽与 widgetsFlow 小组件行对齐、格子居中（内容更宽时以内容宽兜底，避免格子溢出圆角底）。

### D2 显隐状态机（难度 中 · 规模 S）

- 模式键 `extensions.schedule_peek.mode` = `"auto"` | `"always"`。
- **auto**（已确认）：当天至少已结束一节课且当前不在上课（`currentEntry.type !== "class"`）时显示——下课瞬间弹出（滑出 + 淡入动画），进入上课条目即收起；课前/放学后隐藏。
- **always**：当天有课即常驻（含上课期间）。
- 两模式在当天无课（周末/无条目）时均隐藏。
- 状态观察：QML `Connections` 监听 `AppCentral.scheduleRuntime` 的 `currentEntryChanged / currentStatusChanged / currentDayEntriesChanged`（逐属性 NOTIFY 已有；`currentsChanged` 是 C++ 专属信号，QML 不可见）。

### D3 容器插入 + 几何联动（难度 中 · 规模 S）

- `WidgetsContainer.qml` 根 `Column`（L10）内、`widgetsFlow`（L188）之后、`addWidgetsContainer`（L378）之前插入 `SchedulePeekBar`（**上游文件改动，须登记**）。Column 自然排布使其位于小组件正下方；`calcY()` 用的 `height` 是 Column 总高，hide/anchor/偏移自动跟随。
- 显隐/尺寸变化须触发 `contentGeometryChanged`（`WidgetsContainer.qml:39` 信号）→ `MainInterface.qml:182-189` → C++ 重算蒙版。

### D4 蒙版扩展（难度 中 · 规模 S，回归风险最高点）

`WidgetsWindow.cpp`（C++，同步区外，无需登记）：

- `onQmlReady()`（L131）：连接 `schedulePeekBar` 的几何信号（照 `floatingWidgetContainer` 的连接写法，L162-166）；
- `updateMask()`（L216-284）：把 `schedulePeekBar` 的矩形并入 mask（照 L264-275 的写法）——否则速览条被原生蒙版裁掉、不可见不可点。
- 注意保留两条既有语义：`menuShow/editMode/dialogOpen` 时摘整块蒙版；空并集兜底 1×1（L278-282）。

### D5 设置页 SchedulePeek.qml（难度 低 · 规模 S）

显示模式（自动弹出 / 常驻显示）+ 分组间隔阈值滑杆（5~60 分钟）。

### D6 速览验收（难度 低 · 规模 S）

见 §8.4。

## 8. 阶段 E：收尾 + 验证方案

### E1 i18n（难度 低 · 规模 M，偏体力）

- 新 QML 字符串 `qsTr`、C++ 名称 `QCoreApplication::translate("Extensions", ...)`；`app/assets/locales/*.ts` 8 语种 + `lrelease`（未译语种回退英文）。

### E2 QML_MODIFICATIONS.md 完整登记（难度 低 · 规模 S）

登记清单见 §7 文件变更表；同步上游时仅对登记条目做三方合并。

### E3 README/文档（难度 低 · 规模 S）

README 功能描述补「扩展功能」段落，明确与插件（Phase 2）的区别；本文档状态更新为「已实施」。

### E4 全量回归（难度 中 · 规模 S）

CMake/MSVC 全量构建 + 既有冒烟测试模式（`main.cpp`）+ §8.1/8.2 手动清单。

### 8.1 扩展框架

- [ ] 设置窗口出现「扩展功能」页，三项扩展卡片完整；
- [ ] 开关切换即时生效、重启后保持；
- [ ] `extensions.enabled` 落盘正确；`isKeyLocked` 拦截正常。

### 8.2 天气

- [ ] 开关 → 小组件自动出现/消失，「添加小组件」列表同步；
- [ ] 扩展页配置城市 → 组件刷新；数据源/凭据/刷新间隔行为不变；
- [ ] 老配置升级：含天气实例的预设自动启用、城市迁移正确、无损。

### 8.3 随机点名

- [ ] 悬浮按钮可拖动、位置记忆（重启保持）、越界钳位；
- [ ] 面板按按钮位置向左/右展开，「取消」仅收面板；
- [ ] 点 N 名互不重复；权重极端值（-100% 永不中、+100% 明显高频）符合直觉；
- [ ] txt 导入（空行/重名/BOM）、名单增删改、权重滑杆；
- [ ] 两种重复模式行为正确；session 模式关闭结果窗口后重置；
- [ ] 结果窗口居中于 `preferences.display` 选中的屏幕，再点/关闭正常。

### 8.4 课表速览

- [ ] auto 模式下用 `schedule.time_offset`（`ScheduleRuntime.cpp:404`，拨快时钟跨越下课/上课边界）验证弹出/收起时机；
- [ ] always 模式常驻；当天无课隐藏；
- [ ] 缩写回退规则：未设缩写取全名首字、多字缩写取首字；
- [ ] 橙/绿高亮随堂切换正确；
- [ ] `|` 分组随阈值变化；普通课间不插；
- [ ] 速览条可点击（蒙版未裁切）；hide/anchor/编辑模式下几何正确。

## 9. 文件变更与登记清单

**新增（C++，同步区外，无需登记）**：
`src/core/extensions/ExtensionManager.{h,cpp}`、`src/core/extensions/RollCallService.{h,cpp}`（CMakeLists.txt 加入构建）。

**新增（QML，登记为「新增文件」）**：
`pages/settings/Extensions/{Index,Weather,RollCall,SchedulePeek}.qml`、`Components/SchedulePeekBar.qml`、`ClassWidgets/Windows/RollCallFloat.qml`、`ClassWidgets/Windows/RollCallResult.qml`。

**改动（C++，同步区外，无需登记）**：
`AppCentral.{h,cpp}`（装配 + 上下文注册 + rollCall 属性）、`ConfigStore.cpp`（extensions 分区 + 键校验）、`WidgetsModel.{h,cpp}`（definitionsList 过滤）、`AppWindowManager.{h,cpp}`（两窗口）、`WidgetsWindow.cpp`（蒙版）、`BuiltinWidgets.cpp`（天气定义）。

**改动（QML，上游同步区，须逐条登记 QML_MODIFICATIONS.md）**：
1. `Windows/Settings.qml` —— navigationItems 增「扩展功能」项；
2. `Components/WidgetsContainer.qml` —— Column 内插入 SchedulePeekBar + 几何信号联动；
3. `widgets/weather.qml` / `widgets/settings/weather.qml` —— 更新既有「改动 5」条目（项目自有文件：城市改全局、settings 页退役）。

**其他**：`app/assets/locales/*.ts`（8 语种）、`README.md`、本文档。

## 10. 风险与对策

| 风险 | 等级 | 对策 |
|---|---|---|
| 蒙版改动引入"透明窗口遮挡桌面/点击穿透"回归 | **高** | D4 保留既有语义（摘蒙版条件、1×1 兜底）；验收逐项过 §8.4；改动集中在 `updateMask()` 单函数 |
| 上游 QML 同步冲突 | 中 | 改动仅 2 个上游文件且都是增量插入，按 QML_MODIFICATIONS.md 流程登记，三方合并面最小 |
| 老配置迁移破坏用户天气设置 | 中 | 迁移只做"提升"不做"删除"（原实例键保留）；迁移逻辑一次性、幂等、有日志 |
| 多预设语义下实例增删错乱 | 中 | 全部经 `WidgetsModel` 既有槽走持久化通路，不直写配置 |
| 悬浮窗拖拽/展开交互细节反复 | 中 | 直接复用 `FloatingWidgetContainer.qml` 成熟实现（钳位/持久化/TapHandler），不重造 |
| `configs.json` 未声明键松散写入类型漂移 | 低 | `extensions.*` 全部进 `defaultConfig` + `kScalarSpecs`，不走 `weather.*` 的松散先例 |

## 11. 产品决策记录（2026-10-01 评审确认）

1. **天气**（Q1）：开启自动添加天气小组件、关闭自动移除；城市等配置全部集中在「扩展功能-天气」页全局生效；老用户已有天气实例自动迁移为开启。
2. **速览分组**（Q2）：相邻课程间隔 ≥ 阈值（默认 15 分钟，可调）插竖线，普通 10 分钟课间不插。
3. **点名重复**（Q3）：设置项可切换「单次内不重复」/「会话内不重复」。
4. **速览显隐**（Q4）：auto 模式下课弹出、下次上课收起（课间全程可见）。
5. **默认假设（待用户最终确认）**：点名权重映射 = `1 + weight/100`，即 -100% 永不抽中、0% 等权、+100% 概率翻倍；单次「点 N 名」内互不重复。

## 12. 命名备注

- 用户需求中「课表速览」与「课表全览」两种称谓混用；本文档统一为**课表速览**（扩展名与配置页同名）。如需在侧栏显示为「课表全览」，仅改文案即可。
- 扩展 id 前缀 `classwidgets.ext.*` 与小组件 id `classwidgets.*`、插件键 `plugins.*` 三者互不混淆。

## 13. 实施记录（2026-10-01 完成）

- §3 执行总表 27 项任务（A1–E4）全部完成；CMake/MSVC 全量构建与冒烟测试通过，
  §8.1–8.4 验收项经检查清单核验。
- **E1 i18n**：8 语种 `.ts` 各新增 99 条、`.qm` 经 lrelease 重新生成（无 error）。
  context 覆盖 `Extensions`（C++ 名称/描述）、`Weather`、`RollCall`、`SchedulePeek`、
  `RollCallFloat`、`RollCallResult`、`Index`、`Settings`、`weather`。`zh_CN`/`zh_SIMPLIFIED`
  恒等中文、`zh_HK` 繁体、`ja_JP` 日文、`en_US` 中文源条目给英文译文（英文源条目回退
  源文）；`it`/`lzh`/`ta` 全部留空 `unfinished` 回退源文。手工编辑 .ts、未跑 lupdate
  （避免全项目 location churn），既有条目与 `vanished` 条目未动。
- **E2/E3**：`QML_MODIFICATIONS.md` 登记为「改动 12」（上游改动 2 处：Settings 导航项、
  WidgetsContainer 速览条挂载；新增 QML 7 个）并更新「改动 5」天气迁移/退役说明；
  README 补「扩展功能」小节。版本号由主控统一处理，本记录未动 `CMakeLists.txt` 版本。
- **与计划的偏差点**（均为实现期决策，产品语义不变）：
  1. §7 D1 缩写规则：缩写字段实际挂在**科目库 `subject.simplifiedName`**，而非课表条目
     自身；未设置时回退科目全名首字。
  2. §7 D2 auto 模式显示条件：在「当天至少已结束一节课且当前不在上课」之外增加
     `futureCount ≥ 1`——否则最后一节结束后的 free 态仍会命中显示，与"放学后隐藏"矛盾。
  3. §5 B1 天气 `WidgetDefinition.settingsQml` 置空采用"**不再赋值**"（结构体默认空串）
     实现，而非显式赋空语句；行为与计划一致（右键设置入口禁用）。
  4. §4 A2 `extensions.roll_call.avoid_repeat` 与 `extensions.schedule_peek.mode` 在
     ConfigStore 中以**枚举白名单 ScalarKind** 收紧（`single|session`、`auto|always`），
     非法值拒绝——较计划的裸 string 更严格。
  5. §6 C1 `importNamesFromUrl` 为**纯解析、无副作用**（不写配置），合并去重与写回
     `extensions.roll_call.names` 由设置页 RollCall.qml 完成。
