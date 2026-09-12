# QML 修改记录（上游同步区纪律）

> 移植方案 §9.5：QML 目录是**上游同步区**——能不改就不改，改动必须集中记录在本文档，
> 并定期（建议每月）从上游 `main` 拉取 `src/qml/` 变更做回归。

## 当前状态：`app/src/qml` 仅 4 处 M3 插件入口遮蔽（§0.5.8，见下）

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

## 修改申请流程

1. 尽量不动 QML：能由 C++ 宿主、部署脚本或 vendored 副本解决的，不改上游文件；
2. 确需修改 `app/src/qml/**` 时：在本文件追加条目（文件、原因、与上游的 diff 要点）；
3. 同步上游时：仅对本文档列出的改动点做三方合并。
