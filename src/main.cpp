#include "AppCentral.h"
#include "AppPaths.h"
#include "ConfigStore.h"
#include "CWThemeManager.h"
#include "Logger.h"
#include "SingleInstanceGuard.h"
#include "TrayIcon.h"
#include "Version.h" // CMake configure_file 生成的版本号（CWN_VERSION_STRING）
#include "WidgetsModel.h"
#include "WidgetsWindow.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QFontDatabase>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>

#ifdef Q_OS_WIN
// 与 tray.py ctypes.windll.shell32 的调用等价；显式声明避免引入 shobjidl 头
extern "C" __declspec(dllimport) long __stdcall
SetCurrentProcessExplicitAppUserModelID(const wchar_t *AppID);

namespace {
void setWindowsAppUserModelId()
{
    const QString appId = QStringLiteral("Class Widgets Next");
    SetCurrentProcessExplicitAppUserModelID(reinterpret_cast<const wchar_t *>(appId.utf16()));
}
} // namespace
#else
namespace {
void setWindowsAppUserModelId() {}
} // namespace
#endif

int main(int argc, char *argv[])
{
    // B1（内存优化）图形栈钉死：Step-0 同口径矩阵实测选定 D3D11 + basic 渲染循环
    // （数据见 CWNext-内存优化计划.md §5）。setGraphicsApi 必须先于任何窗口创建；
    // Qt 6 对 render loop 只认 QSG_RENDER_LOOP 环境变量（无公开 setter），仅在用户
    // 未显式设置时注入以保留外部覆盖能力。
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
    if (!qEnvironmentVariableIsSet("QSG_RENDER_LOOP"))
        qputenv("QSG_RENDER_LOOP", "basic");

    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false); // 对应 app.py:42，常驻托盘
    QApplication::setApplicationName(QStringLiteral("Class Widgets Next"));
    QApplication::setOrganizationName(QStringLiteral("ClassWidgets"));
    QApplication::setApplicationVersion(QStringLiteral(CWN_VERSION_STRING));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Class Widgets Next"));
    parser.addHelpOption();
    QCommandLineOption smokeTestOption(
        QStringLiteral("smoke-test"),
        QStringLiteral("CI mode: load QML, verify readiness, exit with status"));
    parser.addOption(smokeTestOption);
    parser.process(app);
    const bool smokeTest = parser.isSet(smokeTestOption);

    setWindowsAppUserModelId();

    AppPaths::instance().initialize();

    cwn::Log::install(AppPaths::instance().logsRoot(), /*fileLogging=*/true);
    cwn::Log::info(QStringLiteral("=== Class Widgets Next (M1 skeleton) starting; "
                                  "root=%1 ===")
                       .arg(AppPaths::instance().root()));

    // 单实例守护（T12）：已有实例时直接退出
    SingleInstanceGuard guard;
    if (!guard.tryAcquire()) {
        cwn::Log::error(QStringLiteral("Another Class Widgets Next instance is running"));
        return 0;
    }

    // RinUI 图标字体（FluentSystemIcons）必须进程级加载，QML Icon 控件按字体名取字形
    const QString iconFont = AppPaths::instance().rinUiRoot()
        + QStringLiteral("/assets/fonts/FluentSystemIcons-Resizable.ttf");
    if (QFontDatabase::addApplicationFont(iconFont) < 0)
        cwn::Log::warn(QStringLiteral("Failed to load icon font: %1").arg(iconFont));

    AppCentral central;
    // CI 冒烟测试禁用首跑教程门（否则全新运行目录下永远只开教程窗口，QML 就绪超时）
    central.initialize(!smokeTest);

    if (central.isWaitingForTutorial()) {
        // 首次运行：只承载教程窗口的事件循环（对应 central.py WAITING_FOR_TUTORIAL 分支；
        // 上游此时不建主窗口/托盘）。教程完成后 QML 写 app.tutorial_completed 并
        // 调 AppCentral.restart() 自启新实例走正常流程。
        cwn::Log::info(QStringLiteral("Event loop: tutorial only"));
        const int code = app.exec();
        central.configs()->save();
        guard.release();
        return code;
    }

    WidgetsWindow widgetsWindow(&central);
    central.setWidgetsWindow(&widgetsWindow);

    // 托盘（T11 + M4 托盘菜单补全）
    TrayIcon trayIcon(&central);
    central.setTrayIcon(&trayIcon); // 托盘菜单/系统通知的全部信号接线在 AppCentral 内完成
    if (trayIcon.isValid()) {
        QObject::connect(&trayIcon, &TrayIcon::togglePanel,
                         &central, &AppCentral::onTrayTogglePanel);
        QObject::connect(&trayIcon, &TrayIcon::editModeRequested,
                         &central, &AppCentral::onTrayEditModeRequested);
    }

    widgetsWindow.run();

    if (smokeTest) {
        // CI 冒烟测试：QML 就绪 → 正常退出(0)；超时未就绪 → 失败(3)
        QObject::connect(&widgetsWindow, &RinUiWindowBase::qmlReady, &app, [] {
            cwn::Log::info(QStringLiteral("[smoke-test] MainInterface.qml loaded and "
                                          "widgetsLoader connected"));
            QTimer::singleShot(2000, [] { QCoreApplication::exit(0); });
        });
        QTimer::singleShot(15000, &app, [] {
            cwn::Log::error(QStringLiteral("[smoke-test] QML did not become ready within 15s"));
            QCoreApplication::exit(3);
        });
    }

    const int code = app.exec();
    cwn::Log::info(QStringLiteral("Event loop finished with code %1").arg(code));

    central.configs()->save();
    trayIcon.cleanup();
    guard.release();
    return code;
}
