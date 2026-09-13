#include "AppWindowManager.h"

// 允许清单内的 include（只读使用）：
// - AppCentral.h：setupQmlContext / widgetsWindow / configs 访问；头文件里仅前置
//   声明，无循环依赖（AppCentral.h 不包含本头文件）。
// - AppPaths.h / Logger.h / RinUiWindowBase.h：路径、日志与 QML 引擎基座。
// - WidgetsWindow.h：C1 共享引擎取主窗口引擎句柄（engine() 为基类方法，
//   派生指针调用需完整类型）。
// - ConfigStore.h：sharedEngineEnabled 读 app.shared_engine 配置键。
#include "AppCentral.h"
#include "AppPaths.h"
#include "ConfigStore.h"
#include "Logger.h"
#include "RinUiWindowBase.h"
#include "WidgetsWindow.h"

#include <QMetaObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QWindow>

AppWindowManager::AppWindowManager(AppCentral *central, QObject *parent)
    : QObject(parent)
    , m_central(central)
{
}

AppWindowManager::~AppWindowManager()
{
    // 对应 central.py:318：清理阶段先 release_all；此处兜底（析构时子对象尚存，
    // RinUiWindowBase 析构也会自行 release，双重保护由 isReleased 保证幂等）
    releaseAll();
}

// ─────────────────────────── QML 槽面（manager.py:43-107）───────────────────────────

void AppWindowManager::openSettings()
{
    open(WindowId::Settings);
}

void AppWindowManager::closeSettings()
{
    releaseWindow(WindowId::Settings);
}

void AppWindowManager::openEditor()
{
    // manager.py:115-120 open_editor：当天存在临时换课时阻断编辑器，
    // 转而弹出「恢复/丢弃」确认对话框
    if (hasTodayClassSwaps()) {
        cwn::Log::warn(QStringLiteral(
            "Blocked opening editor because temporary class swaps exist today"));
        openClassSwapRestoreDialog();
        return;
    }
    open(WindowId::Editor);
}

void AppWindowManager::closeEditor()
{
    releaseWindow(WindowId::Editor);
}

void AppWindowManager::openPlaza()
{
    // 插件窗口遮蔽（移植方案 §0.5.8）：插件广场窗口本阶段不参与构建。
    // 上游 manager.py:125-126 会 open("plugin_plaza")（PluginPlaza.qml +
    // PlazaBridge/MarkdownRenderBridge，均属插件域）；Phase 2 落地后恢复。
    cwn::Log::warn(QStringLiteral(
        "WindowManager.openPlaza(): plugin plaza is disabled in this milestone "
        "(porting plan §0.5.8); request ignored"));
}

void AppWindowManager::closePlaza()
{
    // 同 openPlaza：无窗口可关，no-op
    cwn::Log::warn(QStringLiteral(
        "WindowManager.closePlaza(): plugin plaza is disabled in this milestone "
        "(porting plan §0.5.8); request ignored"));
}

void AppWindowManager::openPluginPlaza()
{
    // manager.py:125 open_plugin_plaza 的兼容别名（M1 stub 槽名）；语义同 openPlaza
    openPlaza();
}

void AppWindowManager::openWhatsNew()
{
    open(WindowId::WhatsNew);
}

void AppWindowManager::closeWhatsNew()
{
    releaseWindow(WindowId::WhatsNew);
}

void AppWindowManager::openSingleInstanceDialog()
{
    open(WindowId::SingleInstance);
}

void AppWindowManager::openClassSwap()
{
    open(WindowId::ClassSwap);
}

void AppWindowManager::closeClassSwap()
{
    releaseWindow(WindowId::ClassSwap);
}

void AppWindowManager::openClassSwapRestoreDialog()
{
    open(WindowId::ClassSwapRestore);
}

void AppWindowManager::closeDebugger()
{
    releaseWindow(WindowId::Debugger);
}

void AppWindowManager::closeThemeLoadError()
{
    releaseWindow(WindowId::ThemeLoadError);
}

void AppWindowManager::classSwapRestoreContinue()
{
    // manager.py:100-102：central.resolve_class_swap_restore(discard=False) 后关闭。
    // discard=False 不动换课数据；上游该方法同时清 central 的「启动期待处理换课」
    // 标记，该状态在 C++ 侧归 AppCentral（M3 主控部分）持有，本阶段无状态可清，
    // 故仅关闭对话框。
    releaseWindow(WindowId::ClassSwapRestore);
}

void AppWindowManager::classSwapRestoreDiscard()
{
    // manager.py:104-107：central.resolve_class_swap_restore(discard=True)（丢弃
    // 今天全部换课 override 并落盘）后关闭对话框。
    QObject *classSwapManager = m_central ? m_central->classSwapManager() : nullptr;
    if (classSwapManager) {
        // ClassSwapManager::discardTodaySwaps（M2，Q_INVOKABLE）；动态调用避免
        // 引入 schedule/ 头依赖
        QMetaObject::invokeMethod(classSwapManager, "discardTodaySwaps",
                                  Qt::DirectConnection);
    } else {
        cwn::Log::warn(QStringLiteral(
            "classSwapRestoreDiscard: ClassSwapManager is not available; skipped discard"));
    }
    releaseWindow(WindowId::ClassSwapRestore);
}

void AppWindowManager::openTutorial()
{
    // central.py:234（教程未完成时优先弹出）的调用点
    open(WindowId::Tutorial);
}

void AppWindowManager::openDebugger()
{
    // central.py:524 的调用点（AppCentral::openDebugger → 独立调试器窗口）
    open(WindowId::Debugger);
}

// ─────────────────────────── C++ API ───────────────────────────

void AppWindowManager::openThemeLoadError(const QString &failedThemeId, bool recovered)
{
    // manager.py:161-178：ensure → window.set_error_details(failed_theme_id, recovered)
    // → show_when_ready（50ms × 10 次轮询等根窗口就绪）。
    // C++ 的引擎加载是同步的，无需轮询；但 set_error_details 依赖
    // "ThemeLoadErrorDialog" 上下文属性（windows.py:210，C++ 尚未注册 —— 缺口
    // 已在集成报告/QML_MODIFICATIONS.md 备案），失败主题信息暂以日志呈现，
    // 对话框本体照常创建弹出（其根元素自带 failedThemeId/recovered 默认值属性）。
    cwn::Log::warn(QStringLiteral(
        "Theme load error dialog: failedThemeId='%1', recovered=%2 (context property "
        "'ThemeLoadErrorDialog' is not wired yet; see milestone report)")
                       .arg(failedThemeId)
                       .arg(recovered));
    open(WindowId::ThemeLoadError);
}

// ─────────────────────────── 生命周期内部实现 ───────────────────────────

void AppWindowManager::open(WindowId id)
{
    // manager.py:183-196 open()
    RinUiWindowBase *window = ensure(id);
    if (!window) {
        cwn::Log::error(notInitializedMessage(id));
        return;
    }

    QWindow *root = window->rootWindow();
    if (!root) {
        cwn::Log::error(notInitializedMessage(id));
        return;
    }
    // manager.py:190-194：show + raise + requestActivate
    root->show();
    root->raise();
    root->requestActivate();
}

RinUiWindowBase *AppWindowManager::ensure(WindowId id)
{
    // manager.py:198-204 ensure()：取缓存，未创建则走工厂
    const QString key = windowName(id);
    if (RinUiWindowBase *existing = m_windows.value(key))
        return existing;

    RinUiWindowBase *window = createWindow(id);
    if (window)
        m_windows.insert(key, window);
    return window;
}

RinUiWindowBase *AppWindowManager::createWindow(WindowId id)
{
    // C1 共享引擎（CWNext-内存优化计划.md §6）：辅助窗口默认复用主窗口的
    // QQmlApplicationEngine —— 类型缓存与 JS 堆跨开/关复用，消除旧"每窗独立
    // 引擎"模式下的 RinUI 全量重编译（开窗稳态提交 +40MB、瞬时峰值 +138MB）
    // 与关闭销毁引擎的堆碎片残余（+31MB 不回落，Step 0 C2 实测）。
    // 主窗口不存在（首跑教程门、极早的主题错误弹窗）或开关关闭时回退独立引擎。
    if (!m_central) {
        cwn::Log::error(QStringLiteral("AppWindowManager: no AppCentral wired"));
        return nullptr;
    }

    QQmlApplicationEngine *sharedEngine = nullptr;
    if (sharedEngineEnabled()) {
        if (WidgetsWindow *mainWindow = m_central->widgetsWindow())
            sharedEngine = mainWindow->engine();
    }

    RinUiWindowBase *window = sharedEngine
        ? new RinUiWindowBase(sharedEngine, this) // QObject 父子：管理器析构时兜底清理
        : new RinUiWindowBase(this);

    // 上下文注册只做一次：共享引擎已在 WidgetsWindow::run 里注册过全部上下文
    // 属性与 URL 拦截器；重复 addUrlInterceptor 会让同一 URL 被拦截两次
    if (!sharedEngine)
        m_central->setupQmlContext(window->engine());

    // windows.py:20 把 central.retranslate 连到 engine.retranslate（PySide 专属
    // API）；Qt 6 的 C++ QQmlEngine 无公开 retranslate()，翻译重载随 M4 翻译器
    // 里程碑统一处理。

    if (id == WindowId::Tutorial) {
        // windows.py:137-138：Tutorial.setTheme(Theme.Auto)；RinUiWindowBase::setTheme
        // 目前为占位实现（0 = Auto 枚举占位）
        window->setTheme(0);
    }

    // windows.py 各窗口类的 load(CW_PATH / ...)（路径映射见 windowQmlPath）
    const QString path = windowQmlPath(id);
    window->load(QUrl::fromLocalFile(path));

    if (!window->rootWindow()) {
        // 根对象创建失败（QML 异常等）：不留僵尸窗口，open 时按上游 _errors 文案报错。
        // QML 告警已由 RinUiWindowBase 统一接入日志（warnings → cwn::Log）。
        cwn::Log::error(QStringLiteral("Window '%1' QML load failed: %2")
                            .arg(windowName(id), path));
        delete window;
        return nullptr;
    }

    cwn::Log::info(QStringLiteral("Window '%1' created (%2, engine=%3)")
                       .arg(windowName(id), path,
                            sharedEngine ? QStringLiteral("shared")
                                         : QStringLiteral("owned")));
    return window;
}

bool AppWindowManager::sharedEngineEnabled() const
{
    // C1 灰度回退开关（计划 §9.5）：环境变量优先（便于不改配置的应急回退），
    // 其次配置键，缺省开启
    if (const QString env = qEnvironmentVariable("CW2_SHARED_ENGINE"); !env.isEmpty())
        return env != QLatin1String("0");
    if (m_central && m_central->configs()) {
        if (const auto value = m_central->configs()->value(QStringLiteral("app.shared_engine")))
            return value->toBool(true);
    }
    return true;
}

void AppWindowManager::releaseWindow(WindowId id)
{
    // manager.py:206-216 release()
    RinUiWindowBase *window = m_windows.take(windowName(id));
    if (!window)
        return;

    // manager.py:211-213：先同步 hide 根窗口（QML onClosing 已 event.accepted=false，
    // 窗口不会自行关闭，由这里隐藏）
    if (QWindow *root = window->rootWindow())
        root->hide();

    // manager.py:215-216：进入待释放队列，0ms 延迟后完整释放 ——
    // closeSettings() 等多半由 QML onClosing 触发，同步拆 QML 对象树/清组件缓存
    // 会在回调栈内销毁正在执行的 QML 上下文，必须出栈后处理。
    m_pendingReleases.append(window);

    // guard 侦测窗口对象是否已被提前销毁（QPointer<QObject> 只需完整 QObject 类型）
    QPointer<QObject> guard{window};
    QTimer::singleShot(0, this, [this, guard]() {
        // manager.py:218-235 _finish_release / _release_now：仍在待释放队列中才释放
        if (!guard)
            return;
        for (int i = 0; i < m_pendingReleases.size(); ++i) {
            if (m_pendingReleases.at(i) == guard.data()) {
                m_pendingReleases.removeAt(i);
                auto *managed = static_cast<RinUiWindowBase *>(guard.data());
                // window.release()（windows.py:44-77）：hide → releaseResources →
                // 销毁 root 树（RinUiWindowBase::release 内部实现，幂等；C1 共享
                // 模式下引擎与类型缓存保留，自有模式才销毁引擎）
                managed->release();
                // 销毁窗口对象本身（关闭即销毁，重开时重建）
                managed->deleteLater();
                // B3：通知主窗口安排共享引擎的低频 trim（逐出本窗口的不可达组件）
                emit auxiliaryWindowReleased();
                return;
            }
        }
    });
}

void AppWindowManager::releaseAll()
{
    // manager.py:238-245 release_all()
    if (m_windows.isEmpty() && m_pendingReleases.isEmpty())
        return;

    QList<RinUiWindowBase *> windows = m_windows.values();
    m_windows.clear();
    for (RinUiWindowBase *pending : m_pendingReleases) {
        if (pending && !windows.contains(pending))
            windows.append(pending);
    }
    m_pendingReleases.clear();

    for (RinUiWindowBase *window : windows) {
        if (!window || window->isReleased())
            continue;
        if (QWindow *root = window->rootWindow())
            root->hide();
        // _release_now（manager.py:225-236）：release 自带 _released 幂等保护
        window->release();
        window->deleteLater();
    }
}

// ─────────────────────────── 静态映射 ───────────────────────────

QString AppWindowManager::windowName(WindowId id)
{
    // 键名照 manager.py:18-29 _factories 的字符串键（日志可对照上游）
    switch (id) {
    case WindowId::Settings: return QStringLiteral("settings");
    case WindowId::Editor: return QStringLiteral("editor");
    case WindowId::WhatsNew: return QStringLiteral("whatsnew");
    case WindowId::ClassSwap: return QStringLiteral("class_swap");
    case WindowId::ClassSwapRestore: return QStringLiteral("class_swap_restore");
    case WindowId::SingleInstance: return QStringLiteral("single_instance");
    case WindowId::ThemeLoadError: return QStringLiteral("theme_load_error");
    case WindowId::Tutorial: return QStringLiteral("tutorial");
    case WindowId::Debugger: return QStringLiteral("debugger");
    }
    return QString();
}

QString AppWindowManager::windowQmlPath(WindowId id)
{
    // AppPaths::cwRoot() 对应上游 CW_PATH（= 运行时根/src/qml/ClassWidgets），
    // AppPaths::qmlRoot() 对应 QML_PATH（= 运行时根/src/qml）
    const QString cwRoot = AppPaths::instance().cwRoot();
    const QString qmlRoot = AppPaths::instance().qmlRoot();

    switch (id) {
    case WindowId::Settings:
        // windows.py:103 Settings → CW_PATH / "Windows" / "Settings.qml"
        return cwRoot + QStringLiteral("/Windows/Settings.qml");
    case WindowId::Editor:
        // windows.py:111 Editor → CW_PATH / "Windows" / "Editor.qml"
        return cwRoot + QStringLiteral("/Windows/Editor.qml");
    case WindowId::WhatsNew:
        // windows.py:162 WhatsNew → CW_PATH / "Windows" / "WhatsNew.qml"
        return cwRoot + QStringLiteral("/Windows/WhatsNew.qml");
    case WindowId::ClassSwap:
        // windows.py:181-186 ClassSwapWindow → CW_PATH / "Components" / "dialogs" / "ClassSwapDialog.qml"
        return cwRoot + QStringLiteral("/Components/dialogs/ClassSwapDialog.qml");
    case WindowId::ClassSwapRestore:
        // windows.py:193-198 ClassSwapRestoreDialog → 同目录 ClassSwapRestoreDialog.qml
        return cwRoot + QStringLiteral("/Components/dialogs/ClassSwapRestoreDialog.qml");
    case WindowId::SingleInstance:
        // windows.py:169-174 CheckSingleInstanceDialog → 同目录 CheckSingleInstanceDialog.qml
        return cwRoot + QStringLiteral("/Components/dialogs/CheckSingleInstanceDialog.qml");
    case WindowId::ThemeLoadError:
        // windows.py:213-218 ThemeLoadErrorDialog → 同目录 ThemeLoadErrorDialog.qml
        return cwRoot + QStringLiteral("/Components/dialogs/ThemeLoadErrorDialog.qml");
    case WindowId::Tutorial:
        // windows.py:144 Tutorial → CW_PATH / "Windows" / "Tutorial.qml"
        return cwRoot + QStringLiteral("/Windows/Tutorial.qml");
    case WindowId::Debugger:
        // core/utils/debugger.py:15 → QML_PATH / "Debugger" / "MainWindow.qml"
        return qmlRoot + QStringLiteral("/Debugger/MainWindow.qml");
    }
    return QString();
}

QString AppWindowManager::notInitializedMessage(WindowId id)
{
    // manager.py:30-41 _errors 文案逐字对齐
    switch (id) {
    case WindowId::Settings: return QStringLiteral("Settings window not initialized correctly.");
    case WindowId::Editor: return QStringLiteral("Editor window not initialized correctly.");
    case WindowId::WhatsNew: return QStringLiteral("WhatsNew window not initialized correctly.");
    case WindowId::ClassSwap: return QStringLiteral("ClassSwap window not initialized correctly.");
    case WindowId::ClassSwapRestore:
        return QStringLiteral("ClassSwap restore dialog window not initialized correctly.");
    case WindowId::SingleInstance:
        return QStringLiteral("Single Instance Dialog not initialized correctly.");
    case WindowId::ThemeLoadError:
        return QStringLiteral("Theme load error dialog window not initialized correctly.");
    case WindowId::Tutorial: return QStringLiteral("Tutorial window not initialized correctly.");
    case WindowId::Debugger: return QStringLiteral("Debugger window not initialized correctly.");
    }
    return QStringLiteral("Window not initialized correctly.");
}

bool AppWindowManager::hasTodayClassSwaps() const
{
    // manager.py:116 central.has_today_class_swaps()。C++ 侧经 AppCentral::classSwapManager()
    //（QObject*）动态调用 M2 的 ClassSwapManager::hasTodaySwaps，避免依赖 schedule/ 头。
    QObject *classSwapManager = m_central ? m_central->classSwapManager() : nullptr;
    if (!classSwapManager)
        return false;

    bool has = false;
    if (!QMetaObject::invokeMethod(classSwapManager, "hasTodaySwaps",
                                   Q_RETURN_ARG(bool, has))) {
        cwn::Log::warn(QStringLiteral(
            "hasTodayClassSwaps: ClassSwapManager::hasTodaySwaps is not invocable"));
        return false;
    }
    return has;
}
