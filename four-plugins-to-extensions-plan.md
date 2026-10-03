# 四插件移植到 Next 扩展功能计划

> 状态：**已实施（2026-10-02 全量落地，待质检）** —— A→F 已按本文档执行完毕，
> 质检与各种测试（§7：CMake/MSVC 全量构建 + `--smoke-test` 等）待有条件时基于
> 下方“已完成”标记执行。实施记录见 §11。
> 参考先例：`extensions-feature-plan.md`（扩展框架，已实施）、`weather-multi-provider-plan.md`（天气多数据源，已实施）。
> 上游输入：`example/Class-Widgets-2-More_settings-main/`、`example/Class-Widgets-2-rollcall-main/`、`example/classwidgets2tianqi-main/`、`example/cw2-lessons-displayer-master/`。
> ~~约束：用户已明确“落盘后先不要执行”。~~（约束已解除，2026-10-02 用户指令完整落地。）

---

## 1. 目标与落点决策

把四个上游 Python 插件以**官方扩展（Extension，配置键 `extensions.*`，C++/QML 原生，不加载第三方代码）**重实现，沿用扩展三层架构（`ExtensionManager` 注册表 + 设置页 + 功能实现）。**禁止**移植 Python 补丁注入范式（`integrations.py` 字符串锚点 + `.cwplugin_backups` + 整文件替换）与 `libs/` vendored 依赖（pydantic/click 等）。

| # | 上游插件 | ID / 版本 / 体量（实测） | Next 落点（决策） |
|---|---|---|---|
| P1 | More_settings Kryon 扩展设置 | `com.kryon.more_settings` v1.3.4；约 3428 行：`main.py` 651 + `integrations.py` 608 + `installer_backend.py` 840 + `payload_host.py` 382 + `more_settings_config.py` 46 + `qml/settings.qml` 627 + `tools/make_release_notes.py` 161；补丁目标 `WidgetsContainer` / `WidgetLoader` / `AddOverlayMemberDialog` / `EditOverlayDialog` / `eventCountdown` / `Time.qml` | **新增扩展** `classwidgets.ext.displayTweaks`（显示与小组件增强），单扩展收敛 4 组功能，避免注册表膨胀；远端 payload 安装器舍弃，改为静态编译 |
| P2 | rollcall 随机点名 | `com.rollcall` v2.0.1；2038 行 PY + 1182 行 QML：`main.py` 1238 + `secrandom_service.py` 626 + `secrandom_ipc.py` 174 + `rollcall-button.qml` 284 + `rollcall-result.qml` 283 + `settings.qml` 615 | **增强现有** `classwidgets.ext.rollCall`（Next 已有约 1172 行：`RollCallService` 387 + `RollCallFloat` 285 + `RollCallResult` 177 + 设置页 323；补缺口，不建新 ID） |
| P3 | tianqi 天气(NMC) | `com.weather` v1.2.1；1513 行 + 62KB：`main.py` 702 + `weather.qml` 371 + `weather-settings.qml` 434 + `cities.js` 62KB 单行 | **增强现有** `classwidgets.ext.weather` + 新增第 6 数据源 `NmcProvider`（免 Key；双源融合降级为单源 + 备源回退） |
| P4 | lessons-displayer 全量展示课程 | `com.yersmagit.lessonsdisplayer` v1.1.0-alpha；6982 行：`main.py` 3532 + QML 3450（13 文件，主条 866 / Toolbar 414 / Overlay 319 等） | **增强现有** `classwidgets.ext.schedulePeek`（Next 已有 439 行：Bar 355 + 设置页 84；先全量条，中期白板展示，画笔三期另立项） |

成功标准（实施时验收）：CMake/MSVC 全量构建 + `--smoke-test` 通过；4 扩展开关即时生效、重启保持（`extensions.enabled` 落盘）；§7 清单全过；`QML_MODIFICATIONS.md` 登记；8 语种回退正确；零 Python 运行时依赖；关闭/卸载扩展无残留补丁。

## 2. 现状锚点（Next 已实施部分，直接复用）

- `src/core/extensions/ExtensionManager.{h,cpp}`（71 + 228 行）：`definitions()` 3 项 + `extensions` / `isEnabled` / `setEnabled` / `extensionToggled`，上下文名 `Extensions`。
- `src/core/extensions/RollCallService.{h,cpp}`（88 + 299 行）：名单 `[{name, weight -100~100}]`、有效权重 `1+weight/100`、single/session、`importNamesFromUrl` 纯解析 + `mergeNames` 批量、`draw` / `lastDraw` / `clearSession`。
- `src/core/weather/` 共约 2637 行：`WeatherService` 511 + `WeatherProvider` 154 + `WeatherCodes` 349 + 5 Provider 约 1623（Xiaomi 默认免费 / Amap / Qweather / Weathercn 付费 / Caiyun）；城市已收敛全局 `weather.city`，`poll_interval 1800-10800`，预警经 `com.classwidgets.weather.alerts` 只推最高级 + 会话去重，组件本体已摘除预警渲染。
- `Components/SchedulePeekBar.qml`（355 行）：首字格（`simplifiedName` 首字 → 全名首字 → `#`）、分组竖线（阈值 5-60、默认 15）、橙 `#E8833A` / 绿 `#37A05B` 高亮（上课期间下一节不高亮，与上游 `currentState==0` 一致）、`auto（ended>=1 && !inClass && future>=1）/ always`。
- `QML_MODIFICATIONS.md`（314 行）：当前 13 处改动 + 11 新增；上游同步区纪律不变。
- `ConfigStore.cpp`：`extensions.enabled` 数组校验、`extensions.roll_call.names` 归一、`roll_call.avoid_repeat` 与 `schedule_peek.mode` 枚举白名单。

## 3. 执行总表（实施时顺序 × 难度 × 规模）

难度：低 = 平移；中 = 新逻辑但模式清晰；高 = 跨层或新交互。规模：S（≤1 文件百行级）/ M（数百行）/ L（跨 3 文件或新窗口）。

| 序 | 任务 | 难度 | 规模 | 依赖 | 说明 |
|---|---|---|---|---|---|
| P0 | 精读收敛（4 只读子智能体） | 低 | S | — | **已完成 4/4**（rollcall / tianqi / lessons / more_settings 摘要均已回传） |
| A1 | `ExtensionManager::definitions() +1` displayTweaks + 空服务骨架 | 低 | S | P0 | 图标取 Fluent puzzle 系 |
| A2 | `ConfigStore` 键位落地 + 枚举白名单收紧 | 低 | S | A1 | 见 §6 |
| A3 | `Extensions/Index` 卡片 + `DisplayTweaks.qml` 占位 + 框架验收 | 低 | S | A2 | 开关落盘、重启保持 |
| B | P1 显示增强（动画/几何/不隐藏先行，堆叠 overlay 二期） | 中 | L | A3 | 见 §4 |
| C | P2 点名增强（一期内置差距，二期 SecRandom） | 中 | L | A3 | 见 §4 |
| D | P3 NMC 天气源（单源 + 备源回退，不抢默认源） | 中 | M | A3 | 见 §5 |
| E | P4 课程全量一期（白板展示二期，画笔套件三期另立项） | 高 | L | A3 | 见 §5 |
| F1 | i18n（8 语种 + lrelease） | 低 | M | B-E | `qsTr` + `translate("Extensions"/...)` |
| F2 | QML_MOD 登记 + README + 全量回归 | 低 | S | F1 | 改动 13，冒烟 + §7 清单 |

推荐顺序：A → D → C → B → E → F。D 最小，先验证 Provider 扩展范式；C 增量风险低；B 碰 `WidgetsContainer` 几何；E 蒙版 + 全屏窗口风险最高，放最后。

## 4. P1 More_settings → displayTweaks / P2 rollcall → 增强 RollCall

### 4.1 P1（去补丁化是核心，堆叠 overlay 最贵）

- 组件动画：`BuiltinWidgets` 时间/倒数日加 `animEnabled` 等 flag（时间：秒 / 年月日星期 / `side_by_side|alternate` / 交替间隔 / 淡入淡出 / 滚动开关；倒数日：`countdown_animation`），QML 用 `Behavior enabled:` 包裹，**不整文件替换**。
- 几何：`WidgetsContainer.hideMargin → hideDepthOverride>=0 ? override : 24（macOS 48）`，顶部三停靠 `y → displayTop>=0 ? displayTop : offset_y`；300ms Timer 轮询改为 C++ 绑定/信号直驱；`WidgetsWindow::updateMask` 并入（复用速览 D4 连接写法）。
- 特定课程不隐藏：`ScheduleRuntime.statusChanged → singleShot(0)`，`status in (class, activity)` 且 `currentSubject.name` 命中排除（≤20，加号达上限禁用）则纠正 `hide.state / mini_mode = false`；吸取上游 1.3.4 误读 `current_subject` 教训，经 `ScheduleRuntime.currentEntry` 取名。
- 堆叠 overlay（二期，最复杂）：`WidgetsModel` 加 1 role（不改既有 9 slot 签名）+ `overlayEditingId / overlayListMode` 选中态 + 独占行 / 就地编辑行 / `AddOverlayMemberDialog` 新增文件 / 右键拦截 / presets 摆放 + `removeInstance` 卸载保护。
- 安装器 / 注入检测舍弃：`installer_backend`（远端 manifest + PEP440 + sha256 + zip-slip + 事务验收）改为静态编译随包；`FEATURE_PATCH_SPECS` 改为启动健康自检（失败打 `Logger` + 设置页黄条，不要求卸载）。
- 配置键（`extensions.display_tweaks.*`，由 `plugins.configs.com.kryon.more_settings` 迁移）：`countdown_animation / time_animation / time_show_seconds / time_show_date / time_show_year / time_show_month / time_show_day / time_show_weekday / time_title_mode / time_alternate_interval / time_alternate_animation / display_height(-1) / hide_depth(24) / hide_excluded_enabled / hide_excluded_subjects(JSON)`。

### 4.2 P2（分两期，权重迁移 + docx + SecRandom 是坑点）

- 一期（内置差距）：按钮尺寸 40-160 × 30-100 + 浮窗/实心样式 + `click_hide` + 本节隐藏（runtime tick + 1h 兜底恢复）；结果窗 60ms flicker + `animation_seconds 1-10` + 提前结束 + 金色 `#FFE08A` OutBack 回弹 + 可拖动 + 右下 18px 缩放手柄 + 位置持久化；灵动通知经 `NotificationModel`（停留 2-15s，播报前临时恢复 hide 层/按钮，播后还原）；权重语义统一为 Next `-100~+100`，上游 `1-100默认100` 按 `w_new=(w_old-100)*2/100` 近似迁移，UI 兼容显示；导入加 docx（`QZipReader` 解 `word/document.xml` 取 `<w:t>`，按 `<w:p>/<w:br>` 转行）+ 去序号（`^\s*(\d+[.．、]|\(\d+\)|第\d+名)`）+ 行内分割（`,，;；、\t双空格`）+ `#` 注释 + 编码 `utf-8-sig/utf-16/gbk`；加 `resetWeights / testDraw`。
- 二期 `SecRandomBridge`（仅 Windows 条件编译，失败隔离只打日志）：注册表 HKCU/HKLM + 扫 C-J 盘 + 版本判定 + 三格式记录（`history/<班级>.json` / `roll_call_record__*.json` / `roll_call_record_default.json`）+ 快照基线 1500ms 轮询只读监听 + `secrandom://` trigger + 分代路径 `secrandom_paths{2,3}`；设置页三选一（builtin / 2 / 3）。
- 配置键：`extensions.roll_call.*` 新增 `button_w/h / float_mode / click_hide / animation_seconds / mode / notify_duration / service / ...`；`AppWindowManager` 复用既有两窗口 ID，不新增。

## 5. P3 NMC 天气源 / P4 课程全量展示

### 5.1 P3：新增 `weather/providers/NmcProvider.{h,cpp}`（约 300-400 行）

- 免 Key：`isConfigured` 恒 true（`testConnection` 用 `GET /province/all` 最小请求）；UA 伪装 Chrome。
- 城市索引：`GET /province/all` + 并行 `GET /province/{code}` 建 `[{city,code,province}]`，会话缓存 + 落盘 `configs/weather/nmc_station_index.json`（插件目录旧文件迁移一次；纯数字旧码强制重建字母码）；匹配 精确 > 去后缀（11 种：特别行政区/自治区/自治州/自治县/自治旗/地区/盟/市/区/县/省）> 包含 > 省名回退取省内首城。
- 实况 + 预报：`GET /weather?stationid={code}`（real + predict.detail[0]），哨兵 9999/999 过滤，温度 ±60°C、湿度 0-100 校验，体感差 >20 疑似华氏则换算，夜间最高温 9999 用实况兜底 + 当日最高温 day-cache，强制 `hi>=lo`。
- 预警：`GET /findAlarm?pageNo=1..3&pageSize=50&province={省}`，`city in title` 过滤，红 0 < 橙 1 < 黄 2 < 蓝 3 < 白 4 排序取前 3 → 经灵动通知只推最高 1 条（与现有策略对齐）；NMC 无 alertId，合成 `hash(title+time)` 供会话去重；中文 info → `WeatherCodes` 新增映射；`wind.direct+power → windScale` 文本，`windSpeed` 留空。
- 双源融合降级：`weather.com.cn`（`toy1/search` 取 code + `d1/weather_index/{code}.html` 解析 `var dataSK/cityDZ`）只作 NmcProvider 内部备源回退（描述 `-` 时兜底、湿度/最低温互补、温差 >8° 仍用 NMC），不拆第 7 源，不改门面降级逻辑。
- 设置页 `Weather.qml`：数据源加 `NMC（免Key）` + `weather.auto_location bool`（IP 双源 `api.vore.top` → `ip-api.com`，超时 8s×2，失败回退上次城市）+ 三级城市选（`cities.js` 62KB 转 JSON 资源，或 QML 侧选后回写 `nmcCode`）；`poll_interval` 对齐现有 1800-10800s 钳制；逐项 `show_*/font_*` 与自适应框宽**暂缓**（与固定版式冲突，二期再议）。
- `CityInfo` 加可选键 `nmcCode`，旧数据兼容。

### 5.2 P4：增强 `schedulePeek`（分三期，本计划只含一期）

- 一期全量条（约 +800-1000 行 QML）：`SchedulePeekBar` 加 `displayMode peek|full`；full 显示 class/activity/preparation（仍隐藏 break + 上游 5 类 activity 标题：大课间/升旗/晚读晚练/备考/进考场），placeholder 空态（“今天还没有课程~”），全名展开 + remaining 倒计时，`gap>=15min` 插 separator 渐变竖条，`ListView` 横向 + `computeTargetX` 左 20% 定位 + 400ms 动画 + 用户拖拽暂停 4s + 后端 1s `scrollRequested`。
- 二期白板 / 熄屏展示（约 +400 行，另排期）：新增 `Windows/LessonsBoard.qml` 单窗双主题（纯白/纯黑，课程条固定 4,4 全宽，expanded 详情 + 大字号倒计时），`AppWindowManager::WindowId` 加 `LessonsBoard`，独立 `Frameless+Tool+StaysOnTop` 不进主窗口蒙版；`device_type / widgets_layer / countdown_style / auto_close_*` 键；光标 idle 4s 隐藏。
- 三期画笔套件（约 +2500 行，另立项）：QML `Canvas` 重写 Catmull-Rom→bezier 平滑、8 逻辑色 light/dark 映射、2/4/6px + 橡皮 32px Clear、圈选 OddEvenFill + 蓝框把手平移/等比缩放、`StrokeStore` SQLite + `pages.json` 最多 10 页 + 撤销栈；本计划只预留 `boardStrokes` 落盘路径与工具栏挂载点。
- 设置页 `SchedulePeek.qml`：一期加 `display_mode`，其余沿用 `mode + split_gap_minutes`。

## 6. 公共契约 / 数据流 / 文件清单

- `ExtensionManager::definitions()` +1（`classwidgets.ext.displayTweaks`，图标 Fluent puzzle 系）；天气/点名/速览三定义不动。
- `ConfigStore::defaultConfig + kScalarSpecs`：`extensions.display_tweaks.*`（§4.1 15 键）；`extensions.roll_call.*`（+7：`button_w/h/float_mode/click_hide/animation_seconds/mode/notify_duration/service`，枚举收紧）；`weather.provider` 枚举 +`nmc`，新增 `weather.auto_location bool`，`CityInfo.nmcCode` 可选；`extensions.schedule_peek.*`（+`display_mode/board_*`，一期仅 `display_mode`）。
- `AppCentral`：加 `displayTweaks` 属性（QML `DisplayTweaks`）+ NmcProvider 装配 + Board 窗口接线预留；`WeatherService` start/stop 随天气扩展开关语义不变。
- `WidgetsWindow::updateMask`：并入 P1 高度/深度几何与 P4 一期 full 几何（复用速览 D4 连接写法）；点名/白板全屏窗独立，不并入。
- QML 上游同步区（记为改动 13）：改 2 处（`Components/WidgetsContainer.qml` 高度属性 + full 挂载点；`Settings.qml` 无需加导航项，Index 卡片自现）；新增 4 个（`pages/settings/Extensions/DisplayTweaks.qml`、`Components/dialogs/AddOverlayMemberDialog.qml`、`Windows/LessonsBoard.qml` 二期、`Components/LessonsFullBar.qml` 或并入 Bar，二选一实施时注明）。
- C++ 新增：`DisplayTweaksService`、`weather/providers/NmcProvider`、（二期）`SecRandomBridge`、（三期）笔画存储。
- 其他：`app/assets/locales/*.ts` 8 语种、`README.md` 扩展小节、本文档。

## 7. 验证方案（✅ 已完成条件验证；全量构建与冒烟待有条件时执行）

- 通用：CMake/MSVC 全量构建 + offscreen `--smoke-test` + `lrelease` 无 error + 开关/落盘/重启 + `isKeyLocked`。
- P1：动画开关即时生效；高度/深度重启保持；排除科目 ≤20 上限禁用加号；overlay 成员增删；健康自检黄条。
- P2 一期：尺寸/样式/点击隐藏/课时隐藏恢复；滚动 + 提前结束 + 回弹 + 缩放 + 再点；通知时长 + 隐藏层暂显还原；txt/docx 去序号多编码；single/session；权重迁移正确。
- P2 二期：SecRandom 探测/版本/分代路径/只读监听不播历史。
- P3：NMC 免 Key 拉取成功；IP 定位/手动三级城市；晚间 hi 缓存；预警 Top1 推送；切回小米/高德行为不变。
- P4 一期：peek/full 切换；橙/绿高亮（上课中 next 不亮）；>15min 分割线；滚动定位；hide 联动下可点击。

## 8. 风险与分期 guardrail

| 风险 | 等级 | 对策 |
|---|---|---|
| 蒙版/几何回归（`updateMask` + Column 几何） | 高 | 改动集中单函数，保留摘蒙版条件 + 1×1 兜底；`time_offset` 拨钟验证；E 放最后 |
| 权重/城市旧数据迁移损坏 | 中 | 只提升不删除，幂等 + 日志；大名单 `mergeNames` 批量单次落盘 |
| NMC 首建索引约 30 请求 | 中 | 后台线程 + 进度提示，失败回退手动选 |
| SecRandom / 画笔范围蔓延 | 中 | 严格分期：本计划一期必做，二三期另立任务；本期只留挂载点 |

## 9. 子智能体并行策略（实施时）

- P0 只读 4 路已并行完成（结论即本计划 §1/4/5 输入）。
- 实施期 A3 完成后派 4 实现子智能体并行：`agent-display`（P1，写域 `DisplayTweak* + WidgetsContainer + BuiltinWidgets`）、`agent-rollcall`（P2 一期，`RollCall*`）、`agent-weather`（P3，`Nmc* + Weather.qml`）、`agent-board`（P4 一期，`Peek* + LessonsFull*`）；写域互斥，`ConfigStore / ExtensionManager / AppWindowManager / WidgetsWindow` 由主控串行合入；依赖 `blocked_by: A3`。

## 10. 产品假设（默认决策，评审可翻转仅改常量/文案）

1. P1 单扩展收敛，不拆 4 扩展；补丁检测语义转为健康自检。
2. P2 权重保留 Next `-100~+100` + 上游 1-100 迁移公式。
3. P3 NMC 不抢默认源（默认仍小米），仅新增可选源。
4. P4 画笔用 QML Canvas + JSON 落盘；自适应框宽与逐项字号暂缓。

## 11. 实施记录（2026-10-02 全量落地，待质检）

> 本节为实施后追加：每项标记已完成状态，有条件时按 §7 逐项质检。

| 序 | 任务 | 状态 | 落点摘要 |
|---|---|---|---|
| P0 | 精读收敛 | ✅ 已完成（计划落盘前） | 4/4 摘要即本文 §1/4/5 输入 |
| A1 | displayTweaks 注册 + 空服务骨架 | ✅ 已完成 | `ExtensionManager::definitions() +1`（Fluent puzzle 系图标）+ `DisplayTweaksService.{h,cpp}`（健康自检 + 排除科目解析）+ `AppCentral.displayTweaks`/`DisplayTweaks` 上下文 + `CMakeLists` 注册 |
| A2 | ConfigStore 键位 + 白名单 | ✅ 已完成 | `display_tweaks` 15 键 + `roll_call` 7 键 + `schedule_peek.display_mode/board_strokes` + `weather.auto_location` + 枚举白名单 5 组 + 数值钳位 + `migrateMoreSettingsConfig`（含旧版逗号分隔兼容） |
| A3 | Index 卡片 + DisplayTweaks.qml + 框架验收 | ✅ 已完成 | `DisplayTweaks.qml` 5 组卡 + 健康黄条；Index 页零改动（注册表自现） |
| B | P1 显示增强 | ✅ 已完成（一期；堆叠 overlay 二期挂载） | 动画（`AnimatedDigits.animEnabled` + Time/倒数日透传 + 交替间隔/淡入淡出）/ 几何（hideDepth/displayTop 覆盖）/ 特定课程不隐藏（`currentEntry` 取名纠正）；overlay：`WidgetsModel.OverlayMemberRole` +1 role（不改既有 9 slot 签名）+ 选中态 + 卸载保护 + `AddOverlayMemberDialog.qml` |
| C | P2 点名增强 | ✅ 已完成（一期；SecRandom 二期桩） | docx（zip 解析 + zlib 解压 + `<w:t>` 按 `<w:p>/<w:br>` 转行）+ utf-8-sig/utf-16/GBK + 去序号 + 行内分割 + `#` 注释 + 上游权重迁移公式 + `resetWeights`/`testDraw`/`announce`；悬浮窗尺寸/样式/`click_hide`/上课隐藏（1h 兜底）/通知播报；结果窗 flicker/`animation_seconds`/提前结束/金色回弹/拖动/缩放/持久化；`SecRandomBridge` Windows 条件桩 + 设置页三选一 |
| D | P3 NMC 天气源 | ✅ 已完成 | `NmcProvider`（站号索引 + 会话/落盘缓存 + 精确/去后缀/包含/省回退匹配 + 实况预报校验 + 预警 Top3 + 内部备源回退）+ `CityInfo.nmcCode` + `autoLocate`（IP 双源）+ 设置页 NMC/`auto_location`/三级选后回写；`poll_interval` 钳制沿用既有链路；逐项字号/自适应框宽暂缓（§5.1） |
| E | P4 课程全量一期 | ✅ 已完成（一期；白板二期挂载，画笔三期另立项） | `display_mode peek|full` + full（class/activity/preparation，隐藏 break + 5 类标题；空态占位；全名 + 剩余倒计时；gap 分割渐变竖条；横向 ListView + 左 20% 定位 + 400ms + 拖拽暂停 4s + 1s `scrollRequested`）+ `LessonsBoard.qml` 挂载 + `AppWindowManager.LessonsBoard` + `board_strokes` 预留；蒙版沿用速览条既有并入（同对象） |
| F1 | i18n | ✅ 已完成 | `lupdate` 提取 82 条新串；`zh_CN`/`zh_SIMPLIFIED`/`zh_HK` 全译，`en_US` 中文源英译；`lrelease` 8 语种无 error（`lzh.ts` 因语言标签不被 lupdate 6.8 识别未自动更新，新条运行时回退源文） |
| F2 | QML_MOD + README + 回归 | ✅ 已完成（条件回归；全量构建/冒烟待环境） | `QML_MODIFICATIONS.md` 改动 17 + `README.md` 四扩展小节；条件验证：`lupdate` 无 error、`qmllint` 13 文件零 error、`g++ -fsyntax-only` 全 C++ 通过（`AppCentral.cpp` 除外：预存 `TrayIcon.h → qt_windows.h` Windows-only 依赖，Linux 不可验证，`AppCentral.h` 通过）；CMake/MSVC 全量构建 + `--smoke-test` 待 Qt 6.9 + Windows/MSVC 环境（本机 Qt 6.8/Linux，未执行，未下载工具链） |

质检入口：以本 §11"已完成"标记为基准，按 §7 清单逐项验收；二期（overlay 完整/白板完整）与三期（画笔套件）另立项，不在本轮质检内。

## 12. 质检与修正记录（2026-10-02，MSVC/Qt 6.10.3 本机环境）

首轮 5 路只读质检产出 **61 条发现（P1×8 / P2×17 / P3×36）**，按"P1 全修 + 高价值 P2 修 +
文档失真全修"落地（明细见 QML_MODIFICATIONS.md 改动 18）。要点：

- **P1×8 全修**：displayTweaks 扩展开关门控（Time/eventCountdown/WidgetsContainer，范式
  同速览条）；`migrateMoreSettingsConfig` 改 `migrated` 标记键幂等；SchedulePeekBar full
  模式 `isNext` items/cells 下标混用（TypeError 冻结绑定）；docx 导入三连（zlib 未声明
  → `QZipReader`+`Qt6::CorePrivate` 整链替换、手写 ZIP 负偏移越界、损坏流 inflate 死循环）；
  点名播报"播后还原"Timer 移入 `RollCallService`（应用生命周期 + aboutToQuit 兜底）；
  `weather.auto_location` 启动消费；NMC 数字 cityId 污染 `nmcCode`。
- **高价值 P2/P3**：结果窗几何持久化生效/逐键锁定/负坐标/尺寸恢复；session 全点完
  死锁解除；UTF-16BE 字节序；`<w:tab/>`；健康自检改 C++ 主窗口真实挂载点判定（黄条可达）；
  hide_depth -1 哨兵（平台默认 24/macOS 48 恢复）；死键 `roll_call.mode` 移除；NMC 索引
  路径改 `AppPaths::configsRoot()` + version、数值宽容字符串、有效性判定、预警 3 页、
  白档 rank、他省标题剔除、day-cache 清理、info 映射确定性、备源回退重写（JSONP/文本页
  解析 + alerts 透传）。
- **登记修正**：QML_MODIFICATIONS.md 改动 17 计数（新增 QML 3 个非 4 个/13–17 五节）、
  `scrollRequested` 实为 QML 本地 Timer、macOS 48 描述、相关头注释。
- **验证**（补充质检两轮后最终态）：MSVC 全量重建通过（仅 Logger.cpp 预存 C4834×2）；
  offscreen `--smoke-test` 通过（QML 就绪、退出码 0、日志零 error，且消除了
  `SIGNAL(widthChanged(qreal))` 签名失配告警——QQuickItem 信号无参，已修正）；
  `qmllint` 14 个触及文件 0 error（`WidgetsContainer.qml` 重复 id `dragHandler` 为
  HEAD 预存、运行时作用域正确，不在本轮处理）；`lupdate` 提取 2 条新串
  （"自动"/"最低 %1°"）已译入 zh_CN/zh_SIMPLIFIED/zh_HK/en_US；`lrelease` 8 语种
  0 error，zh 系 1115/1115 全 finished。

### 12.1 补充质检（两轮，按"修复点质量细查"要求执行）

- **第 1 轮（3 路代理）**：点名域 6 项全 PASS；框架/显示域 8 项中 2 项返工
  （Time.qml 关扩展态标题应回上游"交替"基线而非静·并排；migrateMoreSettingsConfig
  的 touched 残留分支不置标记留窄域回滚窗口→整体移除）；**NMC 域判出协议形状
  根本性错位**（响应 `{"data":{...}}` 包装、`real.weather/real.wind` 嵌套、
  `detail[0].day|night.weather.temperature`、findAlarm `data.page.list` +
  `issuetime` + 等级自标题提取、备源 d1 无 `dingzhi/` 前缀、dataSK 键
  `SD/WD/WS`、Referer 必带——均经上游 `example/classwidgets2tianqi-main/main.py`
  实测源码核证），按上游逐字段重修。
- **第 2 轮（1 路代理）**：4 项返工复核，3 PASS + 1 缺陷（d1 正则缺
  `DotMatchesEverythingOption`，对齐上游 `re.S`）已修；另采纳建议：weather.qml
  temperature/tempMax/tempMin 逐键判缺防 NaN°、NMC 风文本与上游一致加空格、
  CMake CorePrivate 缺包显式 FATAL_ERROR、`NmcProvider.h` 补 QHash include。
- **待真机验证项**（静态审查不可达，已留注释）：`day/night.weather.info` 键在
  真实响应中的存在性；d1 页面语句是否跨行；toy1 ref 第 3 段中文名形状；
  findAlarm 省略空参 `signaltype/signallevel` 的服务端行为。
