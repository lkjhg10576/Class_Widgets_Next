#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

class AppCentral;
class RinUiWindowBase;

// 对应上游 src/core/windows/manager.py 的 AppWindowManager（304 行）。
// QML 上下文名固定为 "WindowManager"（central.py:415 注册）——名字注册由
// AppCentral::setupQmlContext 完成（主控负责把 WindowManagerStub 换成本类）；
// C++ 类名用 AppWindowManager 以免与 QML 名混淆。
//
// 生命周期语义（照 manager.py，逐条对齐）：
//   - 每类窗口进程内单例：重复 open → show/raise/activate 已有窗口
//     （manager.py:183-196 open()；窗口对象不重建，只抬升）。
//   - close/release：先同步 hide 根窗口，再经 0ms 延迟做完整释放
//     （QML onClosing 调用栈内不得同步拆 QML 对象树/引擎；manager.py:206-236
//     用 QTimer.singleShot(0, _finish_release) 处理同样的问题）——
//     即「关闭即销毁，重开时重建」，而不是长期 hide 驻留。
//     C1 起默认共享主窗口引擎：销毁指 root 对象树，引擎与类型缓存保留
//     （关闭后 30s 由 WidgetsWindow 的 trim 逐出不可达组件）。
//   - 所有窗口对象以管理器为 QObject 父，管理器析构时兜底清理遗留窗口。
//
// 窗口 QML 源与尺寸/标志（frameless、透明全屏遮罩、FluentWindow 等）
// 全部声明在 QML 根元素上（RinUI FluentWindow / QtQuick Window），C++ 侧
// 只负责按 windows.py 的路径加载 QML，不设置任何窗口标志。
class AppWindowManager : public QObject
{
    Q_OBJECT
public:
    // central：AppCentral 门面（只读使用，见 .cpp include 说明）；parent 允许与
    // central 分开指定（主控通常写 new AppWindowManager(this) 即可）
    explicit AppWindowManager(AppCentral *central, QObject *parent = nullptr);
    ~AppWindowManager() override;

    // 对应 manager.py:238-245 release_all —— central.py:318 的
    // "auxiliary window release" 清理步骤（主控在退出流程调用）
    void releaseAll();

signals:
    // C1 缓存纪律：辅助窗口完成 release（root 树已销毁；共享引擎模式下引擎
    // 保留）后发出。AppCentral 把它接到主窗口 WidgetsWindow::notifyAuxiliaryWindowReleased，
    // 触发共享引擎的低频 trim（脏合并，30s 节拍）。
    void auxiliaryWindowReleased();

public slots:
    // ── QML 槽面：与 manager.py:43-107 的 @Slot 一一对应（方法名逐字一致）──
    Q_INVOKABLE void openSettings();                // manager.py:43
    Q_INVOKABLE void closeSettings();               // manager.py:47
    Q_INVOKABLE void openEditor();                  // manager.py:51（含当天换课阻断）
    Q_INVOKABLE void closeEditor();                 // manager.py:55
    Q_INVOKABLE void openPlaza();                   // manager.py:59 —— 插件广场本阶段警告 + no-op
    Q_INVOKABLE void closePlaza();                  // manager.py:63 —— 同上
    Q_INVOKABLE void openWhatsNew();                // manager.py:67
    Q_INVOKABLE void closeWhatsNew();               // manager.py:71
    Q_INVOKABLE void openSingleInstanceDialog();    // manager.py:75
    Q_INVOKABLE void openClassSwap();               // manager.py:79
    Q_INVOKABLE void closeClassSwap();              // manager.py:83
    Q_INVOKABLE void openClassSwapRestoreDialog();  // manager.py:87
    Q_INVOKABLE void closeDebugger();               // manager.py:91
    Q_INVOKABLE void closeThemeLoadError();         // manager.py:95
    Q_INVOKABLE void classSwapRestoreContinue();    // manager.py:99
    Q_INVOKABLE void classSwapRestoreDiscard();     // manager.py:104

    // 以下三个在上游是 snake_case 普通方法（由 central.py 调用：234/524/160-163），
    // C++ 侧统一暴露为 Q_INVOKABLE（QML 目前未直接调用）：
    Q_INVOKABLE void openTutorial();                // manager.py:152
    Q_INVOKABLE void openDebugger();                // manager.py:155（AppCentral::openDebugger 转发点）
    Q_INVOKABLE void openPluginPlaza();             // manager.py:125 open_plugin_plaza —— no-op 别名
                                                    //（与 M1 SupportStubs::WindowManagerStub 兼容）

public:
    // 对应 manager.py:161-178 open_theme_load_error —— 主题恢复流程入口。
    // 依赖的 "ThemeLoadErrorDialog" 上下文属性（windows.py:210）本阶段未注册，
    // failedThemeId/recovered 暂以日志呈现，窗口本体照常创建并弹出。
    void openThemeLoadError(const QString &failedThemeId = QString(), bool recovered = true);

private:
    // 窗口种类；与 manager.py:18-29 _factories 的字符串键一一对应
    //（plugin_plaza 本阶段刻意缺席，见移植方案 §0.5.8）
    enum class WindowId {
        Settings,
        Editor,
        WhatsNew,
        ClassSwap,
        ClassSwapRestore,
        SingleInstance,
        ThemeLoadError,
        Tutorial,
        Debugger,
    };

    RinUiWindowBase *ensure(WindowId id);           // manager.py:198-204 ensure
    void open(WindowId id);                         // manager.py:183-196 open
    void releaseWindow(WindowId id);                // manager.py:206-236 release
    RinUiWindowBase *createWindow(WindowId id);     // manager.py:247-297 _create_*
    // C1 共享引擎开关：环境变量 CW2_SHARED_ENGINE 优先（"0"=关闭），其次配置键
    // app.shared_engine（缺省开）——计划 §9.5 的灰度回退要求
    bool sharedEngineEnabled() const;

    // manager.py 的窗口键名（用作 m_windows 的键与日志名）
    static QString windowName(WindowId id);
    // windows.py 各窗口类 load() 的 QML 路径 → 本仓库 app/src/qml 映射（含 debugger.py:15）
    static QString windowQmlPath(WindowId id);
    // manager.py:30-41 _errors 的初始化失败文案（逐字对齐）
    static QString notInitializedMessage(WindowId id);
    // manager.py:116 central.has_today_class_swaps()：经 AppCentral::classSwapManager()
    // 以 QMetaObject 动态调用 ClassSwapManager::hasTodaySwaps（M2），避免引入 schedule/ 头依赖
    bool hasTodayClassSwaps() const;

    AppCentral *m_central = nullptr;

    // manager.py:16 self._windows: dict[str, Any] —— 键为 windowName(id)
    QHash<QString, RinUiWindowBase *> m_windows;
    // manager.py:17 self._pending_releases —— 已 close、等待 0ms 延迟释放的窗口。
    // 所有权约定：窗口对象只能由本管理器销毁（父子关系 + deleteLater），裸指针安全。
    QList<RinUiWindowBase *> m_pendingReleases;
};
