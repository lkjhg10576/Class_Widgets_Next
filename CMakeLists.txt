cmake_minimum_required(VERSION 3.21)

project(ClassWidgetsNext
    VERSION 2.0.0.0
    DESCRIPTION "Class Widgets Next - C++/Qt Quick port"
    LANGUAGES CXX)

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
)

qt_add_executable(ClassWidgetsNext WIN32 ${CWN_SOURCES})

target_include_directories(ClassWidgetsNext PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core)

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
endif()

# 运行时资源整体安装：app/ 即运行时根（对应上游 ROOT_PATH）。
# 安装后布局：ClassWidgetsNext.exe + src/qml + assets + RinUI + themes + configs
install(TARGETS ClassWidgetsNext RUNTIME DESTINATION .)
install(DIRECTORY app/ DESTINATION .)
