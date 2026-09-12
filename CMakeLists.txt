cmake_minimum_required(VERSION 3.21)

# ---- 版本段（M5 发布流水线）----
# project(VERSION) 无法被命令行覆盖，故经缓存变量中转：
# 本地构建用默认值 2.0.0.0；CI 发版时传 -DCWN_VERSION_OVERRIDE=x[.y[.z[.w]]]
# 覆盖 project(VERSION) 与下方 configure_file 的 Version.h（格式校验在此完成，
# project(VERSION) 只接受 1-4 段纯数字）。
set(CWN_VERSION "2.0.0.0" CACHE STRING "Class Widgets Next 版本号（x[.y[.z[.w]]]）")
if(CWN_VERSION_OVERRIDE)
    if(NOT CWN_VERSION_OVERRIDE MATCHES "^[0-9]+(\\.[0-9]+){0,3}$")
        message(FATAL_ERROR
            "CWN_VERSION_OVERRIDE 必须为 x[.y[.z[.w]]] 数字格式，收到: ${CWN_VERSION_OVERRIDE}")
    endif()
    set(CWN_VERSION "${CWN_VERSION_OVERRIDE}")
endif()

project(ClassWidgetsNext
    VERSION ${CWN_VERSION}
    DESCRIPTION "Class Widgets Next - C++/Qt Quick port"
    LANGUAGES CXX)

# 生成构建目录下的 Version.h（CWN_VERSION_STRING / MAJOR / MINOR / PATCH / TWEAK），
# 供 src/main.cpp 注入 QApplication::setApplicationVersion
configure_file(src/Version.h.in "${CMAKE_CURRENT_BINARY_DIR}/Version.h" @ONLY)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# M1 目标平台为 Windows（见 docs：Qt 不支持 Linux->Windows 交叉编译，
# Windows 产物由 windows-latest runner 产出）。
find_package(Qt6 6.9 REQUIRED COMPONENTS
    Core Gui Widgets Qml Quick QuickControls2 Network Svg)

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
    src/core/RinUiWindowBase.cpp
    src/core/WidgetsWindow.cpp
    src/core/TrayIcon.cpp
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
)

qt_add_executable(ClassWidgetsNext WIN32 ${CWN_SOURCES})

target_include_directories(ClassWidgetsNext PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_BINARY_DIR})  # 生成的 Version.h

target_link_libraries(ClassWidgetsNext PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets   # QSystemTrayIcon 在 Widgets 模块；体积优化（QML 对话框替代）留到后续里程碑
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Network   # M4 更新器 UpdaterBridge 的 HTTP 检查
    Qt6::Svg
)

if(WIN32)
    target_link_libraries(ClassWidgetsNext PRIVATE shell32)  # AppUserModelID
    target_link_libraries(ClassWidgetsNext PRIVATE winmm)    # 通知音效 PlaySoundW/waveOutSetVolume
endif()

# 运行时资源整体安装：app/ 即运行时根（对应上游 ROOT_PATH）。
# 安装后布局：ClassWidgetsNext.exe + src/qml + assets + RinUI + themes + configs
install(TARGETS ClassWidgetsNext RUNTIME DESTINATION .)
install(DIRECTORY app/ DESTINATION .)
