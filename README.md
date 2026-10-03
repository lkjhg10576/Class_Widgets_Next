# Class Widgets Next

[![Build (Windows)](https://github.com/lkjhg10576/Class_Widgets_Next/actions/workflows/build.yml/badge.svg)](https://github.com/lkjhg10576/Class_Widgets_Next/actions/workflows/build.yml)

**下一代课程表** —— Class Widgets 2 的 C++ / Qt Quick 移植分支。

上游 [Class Widgets 2](https://github.com/RinLit-233-shiroko/Class-Widgets-2) 是
Python (PySide6) 编写的 Windows 桌面课程表小组件；本仓库将其宿主层移植为
C++ + Qt Quick，目标是把安装体积从 ~226 MB 压到 **40–50 MB**、常驻内存压到
**≤140 MB**，同时 **100% 复用上游 21,650 行 QML**（零修改）。

> 当前状态：**2.1.2.0** —— 设置窗口、课表编辑器、主题切换、托盘菜单与
> 5 个官方扩展全部落地；7 个内置小组件（6 个上游对齐 + 倒数日）经 C++ 注册表显示。
> **本版本暂不支持外部插件**（插件系统推迟到 Phase 2，见下）。

## 扩展功能

除小组件外，内置「扩展功能」模块（设置窗口 → 扩展功能）：不绑定小组件的
官方功能单元，独立开关与配置页，配置键统一在 `extensions.*`。当前五项
（实施计划与记录见 [extensions-feature-plan.md](extensions-feature-plan.md)；
四插件移植见 [four-plugins-to-extensions-plan.md](four-plugins-to-extensions-plan.md)；
当日作业见 [homework-extension-plan.md](homework-extension-plan.md)）：

- **天气**（迁入 + 第 6 数据源 NMC）：天气小组件不再以自由添加的内置组件出现，改由扩展开关
  控制——开启自动添加、关闭移除；城市/数据源/API 凭据/刷新间隔等配置收敛
  到「扩展功能-天气」页，写全局 `weather.*` 键，存量实例的 `settings.city`
  自动迁移，无损。数据源新增 **NMC（中央气象台，免 Key）**，默认仍为小米；
  另有 IP 双源自动定位（`weather.auto_location`，失败回退上次城市）。
- **随机点名**（新增，一期增强）：屏幕悬浮可拖动的「点名」按钮（位置记忆），点击展开
  点 1/2/3 名面板，居中结果窗展示抽中名单；名单在扩展页管理（TXT/DOCX 导入、
  去序号、行内分割、`#` 注释、多编码，增删改名、权重 -100%~+100%、
  单次/会话内不重复策略），键
  `extensions.roll_call.*`。悬浮窗尺寸/悬浮-实心样式/点名后隐藏/上课隐藏结果窗
  60ms 滚动 + 金色回弹 + 可拖动/缩放 + 灵动通知播报（**定格后才播**，不泄底）；
  SecRandom 二代/三代为 Windows 条件桩（设置页三选一，不可用时回退内置）。
- **课表速览**（新增，全量一期）：小组件组下方的当日课表条，缩写格（首字格、
  当前课橙色、下一课绿色）与**全量条**（`display_mode`：全名 + 剩余倒计时 +
  横向滚动，左 20% 定位）两种形态，自动（下课弹出、上课收起）或常驻显示，键
  `extensions.schedule_peek.*`。全屏白板（`LessonsBoard` 双主题）为二期挂载点，
  画笔套件另立项。
- **显示与小组件增强**（新增，P1 去补丁化）：时间/倒数日数字动画开关、秒与
  日期分量显隐、日期-星期并排/交替、顶部距离与隐藏深度覆盖、特定课程不隐藏
  （≤20 门），键 `extensions.display_tweaks.*`（由旧插件配置
  `plugins.configs.com.kryon.more_settings` 一次性迁移）；堆叠 overlay 为二期
  挂载点（`AddOverlayMemberDialog` + `WidgetsModel.overlayMember` role）。
- **当日作业**（新增，本仓库自有模型）：下课时自动弹出右侧作业浮窗 + 「作业布置」
  灵动通知提醒课代表填写；浮窗可拖动/缩放（标题栏拖动，避免与列表滚动手势冲突），
  底部 `+` 与列表选中双入口编辑/删除，优先级整行变色（橙/蓝/绿），按天文件存储
  （`configs/homework/YYYY-MM-DD.json`，保留最近 1/3/7 天，损坏文件先留底再开新）。
  拖堂延迟 0–10 分钟、锁定、通知与自动展示均可关；科目侧可逐门关闭「需要布置作业」
  （只抑制通知，不抑制浮窗），键 `extensions.homework.*`。

与插件（Phase 2）的区别：扩展是**官方功能模块**，随程序构建，**不加载任何
第三方代码**；配置键空间 `extensions.*` 与插件的 `plugins.*` 严格分离，
不复用 `Plugins.qml` 页面与插件注册通路。Phase 2 插件系统落地时两者并行、
互不占用对方的开关与键位。

## 仓库布局

```
Class_Widgets_Next/
├─ CMakeLists.txt          # CMake 工程（Qt ≥ 6.9，C++20）
├─ src/                    # C++ 宿主层（对应上游 src/core 的 Python 实现）
│  ├─ main.cpp             # 入口：托盘、单实例、冒烟测试模式
│  └─ core/
│     ├─ AppPaths.*        # PathManager 等价物（纯 URI 拼接）
│     ├─ ConfigStore.*     # Configs（configs.json 读写 + 点分 set/isKeyLocked）
│     ├─ WidgetsModel.*    # WidgetListModel 移植（上游 9 role + 9 slot 一字不改）
│     ├─ BuiltinWidgets.*  # IWidgetProvider + 内置 7 个小组件注册表 + backend
│     ├─ weather/          # WeatherService：天气数据门面（小米/高德/和风/华风爱科/彩云/NMC）
│     ├─ extensions/       # 扩展注册表/开关 + 各扩展服务（点名 / 显示增强 / 当日作业）
│     ├─ CWThemeManager.*  # 主题扫描/查询/切换信号面（M3 补拦截器）
│     ├─ AppCentral.*      # 聚合门面 + QML 上下文注册（名字与上游逐字一致）
│     ├─ RinUiWindowBase.* # pip RinUI 的 Python 层等价物（每窗口引擎 + release 语义）
│     ├─ WidgetsWindow.*   # 主窗口：全屏透明 + 窗口 mask + 33ms 悬停轮询
│     ├─ TrayIcon.*        # 托盘图标与菜单
│     ├─ SingleInstanceGuard.*  # QLockFile 单实例（对应 instance_locker.py）
│     └─ SupportStubs.*    # M2–M4 待落地对象的占位（成员面与 QML 引用对齐）
└─ app/                    # 运行时根（对应上游 ROOT_PATH）
   ├─ src/qml/             # 上游 QML —— 零修改，见 QML_MODIFICATIONS.md
   ├─ src/themes/          # 上游内置主题
   ├─ RinUI/               # vendored RinUI QML 库（含上游覆盖补丁）
   ├─ assets/              # 图标 / 音频 / 翻译
   ├─ themes/              # 外部主题扫描目录
   ├─ configs/             # 用户配置（configs.json / schedules/ / homework/，运行时生成）
   └─ examples/            # 示例课表
```

## 计划与记录文档

| 文档 | 内容 |
|---|---|
| [extensions-feature-plan.md](extensions-feature-plan.md) | 「扩展功能」框架 + 天气/点名/速览三项扩展的实施计划与验收 |
| [four-plugins-to-extensions-plan.md](four-plugins-to-extensions-plan.md) | 四个上游 Python 插件移植为官方扩展（显示增强/点名增强/NMC/课程全量） |
| [homework-extension-plan.md](homework-extension-plan.md) | 当日作业扩展的详细计划、数据安全约定与实施增补 |
| [weather-multi-provider-plan.md](weather-multi-provider-plan.md) | 天气多数据源设计 |
| [upcoming-activities-abbreviation-and-autosize.md](upcoming-activities-abbreviation-and-autosize.md) | 即将上课组件缩写与宽度自适应 |
| [QML_MODIFICATIONS.md](QML_MODIFICATIONS.md) | **上游同步区纪律**：`app/src/qml` 每一处改动与新增文件的登记簿 |

## 构建（Windows）

依赖：**CMake ≥ 3.21**、**MSVC 2022**（或 MinGW-w64）、**Qt ≥ 6.9**
（建议 6.10，与上游 PySide6 版本对齐），模块需含 **qt5compat**
（`Qt5Compat.GraphicalEffects`）。

> ⚠️ 随机点名的 DOCX 名单导入走 QtCore 私有 API `QZipReader`，因此还要求
> **Qt6CorePrivate** 开发组件（`Qt6::CorePrivate` 目标）。缺失时 CMake **配置阶段**
> 即报 `FATAL_ERROR` 而非编译期。私有头随 Qt 小版本变化——CI 与本机同钉
> **Qt 6.10.3**（见 `.github/workflows/build.yml`），升级 Qt 小版本需回归 DOCX 导入路径。

```powershell
# 1. 让程序找到运行时根（指向仓库的 app/ 目录；部署模式下可省略）
setx CW2_APP_ROOT "<仓库路径>\app"

# 2. 配置 + 构建
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel

# 3. 部署运行（开发期直接从构建目录运行即可，AppPaths 会向上查找 app/）
build\Release\ClassWidgetsNext.exe

# 发行版打包：安装 + windeployqt
cmake --install build --prefix dist
windeployqt --release --compiler-runtime --qmldir app dist\ClassWidgetsNext.exe
```

> ⚠️ 手跑 windeployqt 前先导入 MSVC 环境（`. .\scripts\msvc-env.ps1` 后调用
> `Initialize-MSVCEnvironment`），否则它找不到 `VCINSTALLDIR` /
> `VCToolsRedistDir` / `WindowsSdkVerBinPath`，只打一行 warning 就跳过
> MSVC 运行库与 `dxcompiler.dll` / `dxil.dll`，装出来的程序起不来。
> `scripts\build-installer.ps1` 与 CI 都已自动做这一步（并有断言兜底）。

### Windows 安装器（Inno Setup 6）

一键产出全中文向导安装器，依赖本机装有 [Inno Setup 6](https://jrsoftware.org/isinfo.php)
（`winget install JRSoftware.InnoSetup`）：

```powershell
# 完整流程：构建 + 部署 + 打安装器（版本号缺省解析 CMakeLists 的 CWN_VERSION）
scripts\build-installer.ps1

# 已有 dist/ 时只重打安装器；或显式指定版本号
scripts\build-installer.ps1 -SkipBuild
scripts\build-installer.ps1 -Version 2.1.0
```

产物在 `output/ClassWidgets-<版本>-Win-Installer.exe`：

- 全中文向导（语言包 `scripts/Installer_Languages/ChineseSimplified.isl`，官方 Inno
  Setup 6 不含中文，故随仓库 vendor）、安装路径可改、完成页可勾选「立即运行」；
- 装完有开始菜单项与「应用和功能」卸载条目（AppId `com.classwidgets.next`）；
- 卸载与覆盖安装均保留 `{app}\configs`、`{app}\logs` 用户数据 —— 默认
  `configs.json` / 默认课程表仅首次安装落地（`onlyifdoesntexist`），改过的配置不会被重置；
- 默认安装到 `Program Files`（用户数据随之写入安装目录，与上游便携式布局一致）；
  标准权限安装可用命令行 `/currentuser`。

CI：`.github/workflows/build.yml` 在 `windows-latest` 上完成 构建 → windeployqt →
**offscreen 冒烟测试**（`--smoke-test`：QML 就绪即退出，exit 0 = 通过）→ 打包上传
zip 与安装器两个 artifact。

## 运行时根查找规则（AppPaths）

1. 环境变量 `CW2_APP_ROOT`；
2. 从可执行文件目录向上最多 6 级查找标记文件 `src/qml/MainInterface.qml`；
3. 回退到可执行文件目录（部署布局：exe 与 `src/`、`assets/`、`RinUI/` 同级）。

## 契约纪律（移植方案的硬约束）

- 上游 9 个 role 名（`instanceId/typeId/name/icon/qmlPath/backendObj/` `settings/` `settingsQml/widget_id`）
  与 9 个 slot 签名**一字不改** —— 这是将来插件系统把小组件注册回应用的唯一接口；
  堆叠 overlay 需求以**新增**第 10 个 role（`overlayMember`，二期）承接，不动既有 9 个；
- 小组件注册收敛在 `IWidgetProvider` 接口，当前只有内置实现（6 个上游对齐组件
  + 新增倒数日组件；天气已迁入扩展功能），Phase 2 挂 QML/JS 或 Python Sidecar
  实现时零重构；
- QML 上下文属性名（`Configs` / `AppCentral` / `CWThemeManager` / `WidgetsModel` /
  `PathManager` / `WindowManager` / ...）与上游逐字一致；本仓库新增的上下文
  （`Extensions` / `RollCall` / `Homework` / `DisplayTweaks`）一律带扩展前缀语义，
  不与上游命名冲突；
- `app/src/qml` 是上游同步区：**改动必须记录在 [QML_MODIFICATIONS.md](QML_MODIFICATIONS.md)**。

## 许可

- 本仓库：**Apache-2.0**（见 [LICENSE](LICENSE)）
- 上游 QML/资产与 RinUI：**MIT**（Copyright (c) 2026 RinLit），版权与许可声明
  随文件保留，详见 [THIRD_PARTY.md](THIRD_PARTY.md)
- Qt 6：LGPLv3，动态链接，随包提供可替换的 Qt 库文件

## 致谢

- 上游项目与 RinUI：[RinLit-233-shiroko](https://github.com/RinLit-233-shiroko)
- 本项目是社区分支；如需完整功能（插件广场、主题商店、转换器等），请优先支持原版。
