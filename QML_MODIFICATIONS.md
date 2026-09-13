# QML 修改记录（上游同步区纪律）

> 移植方案 §9.5：QML 目录是**上游同步区**——能不改就不改，改动必须集中记录在本文档，
> 并定期（建议每月）从上游 `main` 拉取 `src/qml/` 变更做回归。

## 当前状态：`app/src/qml` 共 8 处改动（4 处 M3 插件入口遮蔽 + 2 处 M5 方法名改写 + 2 处 Interactions 页 sourceSize 移除，见下）
> 另有 2 个**新增文件**（非上游改动）：天气小组件及其设置页，见改动 5。

| 目录 | 内容 | 与上游的差异 |
|---|---|---|
| `app/src/qml/` | 上游 `src/qml/` 全量（137 个 QML、8 个 qmldir，HEAD `1e66f09`） | **4 处（均为插件入口遮蔽，见改动 3）** |
| `app/src/themes/` | 上游内置主题定义 | **无（逐字复制）** |
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
| 5.1 | `widgets/weather.qml` | 新增 | 内置组件 `classwidgets.weather`；backend 为注入的 WeatherService（唯一非通用 backend 的内置组件） |
| 5.2 | `widgets/settings/weather.qml` | 新增 | 城市搜索（350ms 防抖）+ 刷新间隔（全局键 `weather.poll_interval`，秒）+ 数据源标注 |

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

## 修改申请流程

1. 尽量不动 QML：能由 C++ 宿主、部署脚本或 vendored 副本解决的，不改上游文件；
2. 确需修改 `app/src/qml/**` 时：在本文件追加条目（文件、原因、与上游的 diff 要点）；
3. 同步上游时：仅对本文档列出的改动点做三方合并。
