cmake_minimum_required(VERSION 3.21)

# ---- 版本段（M5 发布流水线）----
# project(VERSION) 无法被命令行覆盖，故经缓存变量 CWN_VERSION 中转，
# 同步覆盖 project(VERSION) 与下方 configure_file 的 Version.h。
# 版本号的来源，按优先级：
#   1) CWN_VERSION_OVERRIDE —— 预留的手工覆盖入口（当前 CI 未使用）
#   2) CWN_VERSION 缓存值 —— 缺省即下方默认值
#   3) 无
#
# !! 换版本号必须走 scripts\build-installer.ps1（它会显式传 -DCWN_VERSION=<新版本>）。
#    单独改下面这行的默认值，对已存在的 build/ 无效：CWN_VERSION 是 CACHE 变量，
#    缓存里已有值时 set(... CACHE ...) 的新默认值会被静默忽略（实测：把默认值改成
#    2.1.0.7 后不传 -D 重新配置，产出的仍是旧版本）。要清干净只能删 build/ 重配。
#
# CI（.github/workflows/build.yml）以 -SkipBuild 调用打包脚本，不参与本段配置，
# 其安装器版本号同样取自下面的缺省值 —— 改版本号只需改这一处。
set(CWN_VERSION "2.1.3.1" CACHE STRING "Class Widgets Next 版本号（x[.y[.z[.w]]）")
if(CWN_VERSION_OVERRIDE)
    set(CWN_VERSION "${CWN_VERSION_OVERRIDE}")
endif()

# 校验「最终生效的版本号」，而非只校验覆盖值：各条来源都汇入 CWN_VERSION，
# 在此统一把关才不会漏（早期只校验 OVERRIDE，直接传 CWN_VERSION 会绕过校验）。
# 逐段可选而非 {0,3}：CMake 的 if(MATCHES) 正则引擎不支持 {n,m} 重复量词，
# 写成 {0,3} 会静默不匹配任何值，于是所有 4 段版本号（如 2.1.0.4）都被判为非法。
if(NOT CWN_VERSION MATCHES "^[0-9]+(\\.[0-9]+)?(\\.[0-9]+)?(\\.[0-9]+)?$")
    message(FATAL_ERROR
        "CWN_VERSION 必须为 x[.y[.z[.w]]] 数字格式（project(VERSION) 只接受 1-4 段纯数字），收到: ${CWN_VERSION}")
endif()

project(ClassWidgetsNext
    VERSION ${CWN_VERSION}
    DESCRIPTION "Class Widgets Next - C++/Qt Quick port"
    LANGUAGES CXX)

# 生成构建目录下的 Version.h（CWN_VERSION_STRING / MAJOR / MINOR / PATCH / TWEAK），
# 供 src/main.cpp 注入 QGuiApplication::setApplicationVersion
configure_file(src/Version.h.in "${CMAKE_CURRENT_BINARY_DIR}/Version.h" @ONLY)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# M1 目标平台为 Windows（见 docs：Qt 不支持 Linux->Windows 交叉编译，
# Windows 产物由 windows-latest runner 产出）。
find_package(Qt6 6.9 REQUIRED COMPONENTS
    Core Gui Qml Quick QuickControls2 Network Svg)
# RollCallService docx 导入用 QtCore 私有 API QZipReader（Qt6::CorePrivate 目标）。
# 私有头随 Qt 小版本变化：本项目 CI 与本机同钉 Qt 6.10.3（见 .github/workflows/build.yml），
# 升级 Qt 小版本时需回归 docx 导入路径
find_package(Qt6 COMPONENTS CorePrivate QUIET)
if(NOT TARGET Qt6::CorePrivate)
    message(FATAL_ERROR
        "Qt6CorePrivate 未找到：docx 名单导入依赖 QtCore 私有 API QZipReader"
        "（CI 与本机均为 Qt 6.10.3，请检查 Qt 安装完整性）")
endif()
# 语音播报扩展（tts-extension §4.6）：Qt TextToSpeech 为可选模块（本机/CI 的
# Qt 均未安装该组件）。QUIET 探测：存在则链接真实合成后端；缺失则以
# CWN_NO_TTS 编译桩实现（healthy=false，设置页只读提示），不阻塞主构建。
find_package(Qt6 COMPONENTS TextToSpeech QUIET)
if(TARGET Qt6::TextToSpeech)
    message(STATUS "Qt6 TextToSpeech found: TTS speech backend enabled")
else()
    message(STATUS "Qt6 TextToSpeech NOT found: TTS builds as stub (CWN_NO_TTS)")
endif()

qt_standard_project_setup()

set(CWN_SOURCES
    src/main.cpp
    src/core/AppPaths.cpp
    src/core/Logger.cpp
    src/core/ConfigStore.cpp
    src/core/WidgetsModel.cpp
    src/core/BuiltinWidgets.cpp
    src/core/CWThemeManager.cpp
    src/core/SupportStubs.cpp
    src/core/AppCentral.cpp
    # 扩展功能框架
    src/core/extensions/ExtensionManager.cpp
    src/core/extensions/ExtensionManager.h
    # 随机点名扩展服务（阶段 C1：名单存储 + 加权抽取 + txt 解析）
    src/core/extensions/RollCallService.cpp
    src/core/extensions/RollCallService.h
    # 当日作业扩展服务（按天文件读写 + 过期清理 + 作业布置通知）
    src/core/extensions/HomeworkService.cpp
    src/core/extensions/HomeworkService.h
    # 显示增强扩展服务（four-plugins A1：健康自检 + 排除科目解析）
    src/core/extensions/DisplayTweaksService.cpp
    src/core/extensions/DisplayTweaksService.h
    # 语音播报扩展服务（tts-extension：Qt TextToSpeech 通知朗读 + 模板 + 来源过滤）
    src/core/extensions/TtsService.cpp
    src/core/extensions/TtsService.h
    # P2 二期 SecRandom 桥（仅 Windows 编译，见文件内 #ifdef；Linux 为空桩）
    src/core/extensions/SecRandomBridge.cpp
    src/core/extensions/SecRandomBridge.h
    src/core/RinUiWindowBase.cpp
    src/core/WidgetsWindow.cpp
    src/core/TrayIcon.cpp
    src/core/NativeFileDialog.cpp
    src/core/SingleInstanceGuard.cpp
    # M2 课程表域
    src/core/schedule/UnionTimer.cpp
    src/core/schedule/ScheduleModel.cpp
    src/core/schedule/ScheduleParser.cpp
    src/core/schedule/ScheduleManager.cpp
    src/core/schedule/ScheduleIO.cpp
    src/core/schedule/ScheduleRuntime.cpp
    src/core/schedule/ScheduleEditor.cpp
    src/core/schedule/ClassSwapManager.cpp
    # M5 课表格式转换（CW1/CSES ↔ CW2 内部格式）
    src/core/convertor/ScheduleConverter.cpp
    src/core/convertor/ConvertorBridge.cpp
    # M3 主题系统
    src/core/themes/ThemeLoader.cpp
    src/core/themes/ThemeUrlInterceptor.cpp
    src/core/themes/ThemeImporter.cpp
    src/core/themes/ThemeRecovery.cpp
    src/core/themes/ThemeLoadErrorDialog.cpp
    # M3 窗口管理
    src/core/windows/AppWindowManager.cpp
    # M4 通知
    src/core/notification/NotificationProvider.cpp
    src/core/notification/NotificationService.cpp
    # M4 工具层
    src/core/utils/LogListModel.cpp
    src/core/utils/AutoStartup.cpp
    src/core/utils/Calculator.cpp
    src/core/utils/Translator.cpp
    src/core/utils/UtilsBackend.cpp
    # M4 更新器与自动化
    src/core/updater/UpdaterBridge.cpp
    src/core/updater/UpdateDownloader.cpp
    src/core/automations/AutomationManager.cpp
    src/core/automations/BuiltinTasks.cpp
    src/core/automations/UpdateCheckTask.cpp
    # 天气小组件数据门面 + 多数据源 Provider（小米/高德/和风/华风爱科/彩云/NMC）
    src/core/weather/WeatherService.cpp
    src/core/weather/WeatherProvider.cpp
    src/core/weather/WeatherCodes.cpp
    src/core/weather/providers/XiaomiProvider.cpp
    src/core/weather/providers/AmapProvider.cpp
    src/core/weather/providers/QweatherProvider.cpp
    src/core/weather/providers/WeathercnProvider.cpp
    src/core/weather/providers/CaiyunProvider.cpp
    src/core/weather/providers/NmcProvider.cpp
    src/core/weather/providers/NmcProvider.h
)

qt_add_executable(ClassWidgetsNext WIN32 ${CWN_SOURCES})

target_include_directories(ClassWidgetsNext PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_BINARY_DIR})  # 生成的 Version.h

target_link_libraries(ClassWidgetsNext PRIVATE
    Qt6::Core
    Qt6::CorePrivate  # RollCallService docx 导入的 QZipReader（Qt 内置 zip 解析）
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Network   # M4 更新器 UpdaterBridge 的 HTTP 检查
    Qt6::Svg
)

# 语音播报：模块存在即链接真实后端，否则全工程注入 CWN_NO_TTS 切桩实现
if(TARGET Qt6::TextToSpeech)
    target_link_libraries(ClassWidgetsNext PRIVATE Qt6::TextToSpeech)
else()
    target_compile_definitions(ClassWidgetsNext PRIVATE CWN_NO_TTS)
endif()

if(WIN32)
    target_link_libraries(ClassWidgetsNext PRIVATE shell32)  # AppUserModelID + B4 托盘 Shell_NotifyIcon
    target_link_libraries(ClassWidgetsNext PRIVATE winmm)    # 通知音效 PlaySoundW/waveOutSetVolume
endif()

# 运行时资源整体安装：app/ 即运行时根（对应上游 ROOT_PATH）。
# 安装后布局：ClassWidgetsNext.exe + src/qml + assets + RinUI + themes + configs
install(TARGETS ClassWidgetsNext RUNTIME DESTINATION .)
install(DIRECTORY app/ DESTINATION .)
# UtilsBackend::loadLicense 读 ROOT_PATH/LICENSE（上游 backend.py:227-239），随包提供
install(FILES LICENSE DESTINATION .)
