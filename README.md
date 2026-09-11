# Class Widgets Next

**下一代课程表** —— Class Widgets 2 的 C++ / Qt Quick 移植分支。

上游 [Class Widgets 2](https://github.com/RinLit-233-shiroko/Class-Widgets-2) 是
Python (PySide6) 编写的 Windows 桌面课程表小组件；本仓库将其宿主层移植为
C++ + Qt Quick，目标是把安装体积从 ~226 MB 压到 **40–50 MB**、常驻内存压到
**≤140 MB**，同时 **100% 复用上游 21,650 行 QML**（零修改）。

> 当前状态：**M1 骨架点亮** —— C++ 宿主加载未修改的 `MainInterface.qml`，
> 6 个内置小组件经 C++ 注册表显示，托盘可退出。设置窗口/编辑器/主题切换在
> M2–M3 陆续落地；**本版本暂不支持外部插件**（插件系统推迟到 Phase 2，见下）。

## 仓库布局

```
Class_Widgets_Next/
├─ CMakeLists.txt          # CMake 工程（Qt ≥ 6.9，C++20）
├─ src/                    # C++ 宿主层（对应上游 src/core 的 Python 实现）
│  ├─ main.cpp             # 入口：托盘、单实例、冒烟测试模式
│  └─ core/
│     ├─ AppPaths.*        # PathManager 等价物（纯 URI 拼接）
│     ├─ ConfigStore.*     # Configs（configs.json 读写 + 点分 set/isKeyLocked）
│     ├─ WidgetsModel.*    # WidgetListModel 移植（9 role + 9 slot 一字不改）
│     ├─ BuiltinWidgets.*  # IWidgetProvider + 内置 6 个小组件注册表 + backend
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
   ├─ configs/             # 用户配置（运行时生成 configs.json）
   └─ examples/            # 示例课表
```

## 构建（Windows）

依赖：**CMake ≥ 3.21**、**MSVC 2022**（或 MinGW-w64）、**Qt ≥ 6.9**
（建议 6.10，与上游 PySide6 版本对齐），模块需含 **qt5compat**
（`Qt5Compat.GraphicalEffects`）。

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

CI：`.github/workflows/build.yml` 在 `windows-latest` 上完成 构建 → windeployqt →
**offscreen 冒烟测试**（`--smoke-test`：QML 就绪即退出，exit 0 = 通过）→ 打包上传 artifact。

## 运行时根查找规则（AppPaths）

1. 环境变量 `CW2_APP_ROOT`；
2. 从可执行文件目录向上最多 6 级查找标记文件 `src/qml/MainInterface.qml`；
3. 回退到可执行文件目录（部署布局：exe 与 `src/`、`assets/`、`RinUI/` 同级）。

## 契约纪律（移植方案的硬约束）

- `WidgetsModel` 的 9 个 role 名（`instanceId/typeId/name/icon/qmlPath/backendObj/`
  `settings/settingsQml/widget_id`）与 9 个 slot 签名**一字不改** —— 这是将来插件
  系统把小组件注册回应用的唯一接口；
- 小组件注册收敛在 `IWidgetProvider` 接口，当前只有内置实现（6 个小组件），
  Phase 2 挂 QML/JS 或 Python Sidecar 实现时零重构；
- QML 上下文属性名（`Configs` / `AppCentral` / `CWThemeManager` / `WidgetsModel` /
  `PathManager` / `WindowManager` / ...）与上游逐字一致，137 个 QML 零改动；
- `app/src/qml` 是上游同步区：**改动必须记录在 [QML_MODIFICATIONS.md](QML_MODIFICATIONS.md)**。

## 许可

- 本仓库：**Apache-2.0**（见 [LICENSE](LICENSE)）
- 上游 QML/资产与 RinUI：**MIT**（Copyright (c) 2026 RinLit），版权与许可声明
  随文件保留，详见 [THIRD_PARTY.md](THIRD_PARTY.md)
- Qt 6：LGPLv3，动态链接，随包提供可替换的 Qt 库文件

## 致谢

- 上游项目与 RinUI：[RinLit-233-shiroko](https://github.com/RinLit-233-shiroko)
- 本项目是社区分支；如需完整功能（插件广场、主题商店、转换器等），请优先支持原版。
