#include "WidgetsWindow.h"

#include "AppCentral.h"
#include "AppPaths.h"
#include "CWThemeManager.h"
#include "ConfigStore.h"
#include "Logger.h"
#include "WidgetsModel.h"

#include <QCursor>
#include <QQuickItem>
#include <QQuickWindow>

#ifdef Q_OS_WIN
// kernel32（kernel32.lib 默认链接）：工作集收缩。与 main.cpp 的 extern 声明风格一致，
// 不引入 <windows.h> 以避免与 Qt 头的宏冲突。
extern "C" __declspec(dllimport) int __stdcall SetProcessWorkingSetSize(
    void *hProcess, std::size_t dwMinimumWorkingSetSize, std::size_t dwMaximumWorkingSetSize);
extern "C" __declspec(dllimport) void *__stdcall GetCurrentProcess();
#endif

WidgetsWindow::WidgetsWindow(AppCentral *central, QObject *parent)
    : RinUiWindowBase(parent)
    , m_central(central)
{
    m_mainQmlUrl = QUrl::fromLocalFile(AppPaths::instance().qmlRoot()
                                       + QStringLiteral("/MainInterface.qml"));

    // 对应 core.py:36 的 QueuedConnection
    connect(m_engine.data(), &QQmlApplicationEngine::objectCreated,
            this, &WidgetsWindow::onQmlReady, Qt::QueuedConnection);
}

void WidgetsWindow::run()
{
    // 对应 WidgetsWindow.run：注册 QML 上下文 → 加载 preset → 连主题信号 → 带主题加载主界面
    m_central->setupQmlContext(engine());
    m_central->widgetsModel()->loadConfig();

    // hover_fade 缓存：轮询回调里不能每帧重建整棵配置 QVariantMap。
    // A1 缓存化后 data() 本身零拷贝；A6 起 hover_fade 关闭时轮询定时器直接停止
    const QVariantMap interactions =
        m_central->configs()->data().toMap().value(QStringLiteral("interactions")).toMap();
    m_hoverFade = interactions.value(QStringLiteral("hover_fade")).toBool();
    connect(m_central->configs(), &ConfigStore::dataChanged, this, [this] {
        const QVariantMap interactions =
            m_central->configs()->data().toMap().value(QStringLiteral("interactions")).toMap();
        m_hoverFade = interactions.value(QStringLiteral("hover_fade")).toBool();
        // A6：hover_fade 关闭 → 停止轮询（原先进程生命周期永不停，空转 30 次/秒）
        if (m_hoverFade)
            m_mouseTimer.start(kMousePollIntervalMs);
        else
            m_mouseTimer.stop();
    });

    // 连接要先于首次加载：无效启动主题在失败处理选择默认主题后可立即替换
    connect(m_central->themeManagerObject(), &CWThemeManager::themeChanged,
            this, &WidgetsWindow::onThemeChanged);

    load(m_mainQmlUrl);

    // A6：鼠标悬停检测轮询仅在 hover_fade 开启时运行，间隔 33ms → 100ms
    // （core.py:39-42 的 33ms 常驻轮询是 CPU/唤醒浪费；M3 注释的"事件驱动"
    // 中期方向不变，此处先止血）
    connect(&m_mouseTimer, &QTimer::timeout, this, &WidgetsWindow::updateMouseState);
    if (m_hoverFade)
        m_mouseTimer.start(kMousePollIntervalMs);

    // B3：低频 trim 定时器 —— 平时停表，仅在辅助窗口关闭置脏后运行，
    // trim 后发现仍无脏即自动停表（A6 定时器治理纪律：不空转）。
    // C1 起：辅助窗口与主窗口共享引擎，trim 直接作用于共享缓存。
    m_trimTimer.setInterval(kTrimIntervalMs);
    connect(&m_trimTimer, &QTimer::timeout, this, &WidgetsWindow::onTrimTick);

    // C1 后续（用户采纳）：启动期的一次性 init 页（主题加载/字体预热/配置装载
    // 等摸过一次就不再碰的）也会常驻工作集 —— 启动 15s 后置脏一次，走与
    // "辅助窗口关闭后"完全相同的 trim + 工作集收缩路径（实际收缩落在其后
    // ≤30s 的脏检查节拍上，即启动 ~15–45s 完成），把常驻基线压到热集水平
    // （真机实测：70–80 → 35–40MB）。此后无辅助窗口活动即不再触发。
    QTimer::singleShot(kStartupTrimDelayMs, this, [this] {
        if (!m_released && m_engine) {
            m_trimDirty = true;
            if (!m_trimTimer.isActive())
                m_trimTimer.start();
        }
    });
}

void WidgetsWindow::notifyAuxiliaryWindowReleased()
{
    m_trimDirty = true;
    if (!m_trimTimer.isActive())
        m_trimTimer.start();
}

void WidgetsWindow::onTrimTick()
{
    if (!m_trimDirty) {
        m_trimTimer.stop(); // 无新脏 → 停表等待下一次关闭事件
        return;
    }
    m_trimDirty = false;
    if (!m_engine || m_released)
        return;

    // C1：辅助窗口与主窗口共享引擎，关闭释放的组件类型留在共享类型缓存中。
    // trimComponentCache() 只逐出**不可达**组件（区别于 clearComponentCache()
    // 的全量清空，主界面常驻组件有引用、不受影响），再触发一次 JS GC 回收
    // 碎片，使关闭辅助窗口后的内存回到打开前水平。节拍取 30s：兼顾"用户关掉
    // 窗口后马上看任务管理器"的回落观感与 30s 内重开走缓存的快速路径。
    m_engine->trimComponentCache();
    m_engine->collectGarbage();

#ifdef Q_OS_WIN
    // trim/GC 只是把内存变成"空闲"，Windows 不会把 free 掉的页自动移出工作集
    // （任务管理器"内存"=专用工作集不会自己降）——共享引擎释放的是堆内小块
    // （旧架构引擎整体销毁是 VirtualFree 大块，会立刻去提交）。低频地把工作集
    // 收缩一次，空闲页真正归还系统；再被用到时按需软故障调回，30s 节拍可接受。
    const int trimmed = SetProcessWorkingSetSize(GetCurrentProcess(),
                                                 static_cast<std::size_t>(-1),
                                                 static_cast<std::size_t>(-1));
    cwn::Log::info(QStringLiteral("Working set trimmed after auxiliary window "
                                  "release (SetProcessWorkingSetSize=%1)")
                       .arg(trimmed ? QStringLiteral("ok") : QStringLiteral("failed")));
#endif
    cwn::Log::info(QStringLiteral(
        "B3 main-engine trim: component cache trimmed + JS GC after auxiliary "
        "window release"));
}

void WidgetsWindow::onQmlReady(QObject *obj, const QUrl &objUrl)
{
    if (QUrl(objUrl).adjusted(QUrl::NormalizePathSegments)
        != m_mainQmlUrl.adjusted(QUrl::NormalizePathSegments))
        return;

    if (!obj) {
        const QString failedTheme = m_central->themeManagerObject()->currentTheme();
        cwn::Log::error(
            QStringLiteral("Main QML Load Failed for theme '%1'").arg(failedTheme));
        applyEmptyMask();
        m_qmlReady = false;
        emit themeLoadFailed(failedTheme);
        return;
    }

    if (m_qmlReady)
        return;

    QObject *widgetsLoader = obj->findChild<QObject *>(QStringLiteral("widgetsLoader"));
    if (!widgetsLoader) {
        cwn::Log::error(QStringLiteral("'widgetsLoader' object has not found"));
        applyEmptyMask();
        emit themeLoadFailed(m_central->themeManagerObject()->currentTheme());
        return;
    }

    // QML 声明的信号：geometryChanged / contentGeometryChanged（字符串连接，带参不敏感）
    connect(widgetsLoader, SIGNAL(geometryChanged()), this, SLOT(scheduleMaskUpdate()));
    connect(widgetsLoader, SIGNAL(contentGeometryChanged()), this, SLOT(scheduleMaskUpdate()));

    if (QObject *floatingContainer =
            obj->findChild<QObject *>(QStringLiteral("floatingWidgetContainer"))) {
        connect(floatingContainer, SIGNAL(geometryChanged()), this, SLOT(scheduleMaskUpdate()));
        cwn::Log::info(QStringLiteral("Floating widget container connected for mask updates"));
    }

    scheduleMaskUpdate();
    m_qmlReady = true;
    emit qmlReady();
}

void WidgetsWindow::onThemeChanged()
{
    if (m_themeReloading) {
        cwn::Log::info(QStringLiteral("Theme reload in progress, skipping"));
        return;
    }
    m_themeReloading = true;
    cwn::Log::info(QStringLiteral("Theme changed, starting reload process"));

    CWThemeManager *themeManager = m_central->themeManagerObject();
    const QString currentThemeId = themeManager->currentTheme();
    if (!themeManager->isThemePathValid(currentThemeId)) {
        cwn::Log::error(QStringLiteral("Theme '%1' path is invalid during theme change")
                            .arg(currentThemeId));
        themeManager->themeChange(CWThemeManager::defaultThemeId());
    }

    // 主题组件由 QML 类型缓存解析；必须先清缓存再重建 Loader 内容
    // （core.py _finish_theme_reload；singleShot(0) 防止在 Loader 回调内回收垃圾）
    QTimer::singleShot(0, this, [this] {
        engine()->clearComponentCache();
        cwn::Log::info(QStringLiteral("Theme component cache cleared, emitting reload signal"));
        m_central->themeManagerObject()->themeReadyToReload();
        m_themeReloading = false;
    });
}

void WidgetsWindow::applyEmptyMask()
{
    // 防止失败/未完成的 QML 加载把全屏窗口暴露出来
    m_interactiveRect = QRegion();
    if (QWindow *root = rootWindow())
        root->setMask(QRegion(QRect(0, 0, 1, 1)));
}

void WidgetsWindow::scheduleMaskUpdate()
{
    if (m_maskUpdatePending)
        return;
    m_maskUpdatePending = true;
    QTimer::singleShot(0, this, &WidgetsWindow::updateMask);
}

void WidgetsWindow::updateMask()
{
    m_maskUpdatePending = false;
    QQuickWindow *root = qobject_cast<QQuickWindow *>(rootWindow());
    if (!root)
        return;

    QRegion mask;
    QQuickItem *widgetsLoader = root->findChild<QQuickItem *>(QStringLiteral("widgetsLoader"));
    if (!widgetsLoader) {
        // 空 region 会清掉原生 mask，导致 QML 加载期间整屏透明窗口遮挡桌面
        applyEmptyMask();
        return;
    }

    const bool menuShow = widgetsLoader->property("menuVisible").toBool();
    const bool editMode = widgetsLoader->property("editMode").toBool();
    // B4：MainInterface 常驻弹窗（调休/切换课程表，RinUI Dialog = QQC2 Popup）渲染在
    // 本窗口 overlay 的屏幕中央，位于小组件蒙版之外 —— 不摘蒙版弹窗会被整块裁掉，
    // 用户看到"无弹窗 + 小组件被模态遮罩压暗"的假死。dialogOpen 由 QML 在弹窗
    // visible 变化时经 geometryChanged() 触发本函数（见 MainInterface.qml）。
    const bool dialogOpen = root->property("dialogOpen").toBool();
    if (menuShow || editMode || dialogOpen) {
        m_interactiveRect = QRegion();
        root->setMask(QRegion());
        return;
    }

    // 小组件位于 Flow 内部；浮窗模式切换时仍需保留实际几何
    QQuickItem *widgetsFlow =
        widgetsLoader->findChild<QQuickItem *>(QStringLiteral("widgetsFlow"));
    if (widgetsFlow) {
        const qreal baseX = widgetsLoader->x();
        const qreal baseY = widgetsLoader->y();
        const qreal flowX = widgetsFlow->x();
        const qreal flowY = widgetsFlow->y();

        for (QQuickItem *child : widgetsFlow->childItems()) {
            if (child->width() <= 0 || child->height() <= 0 || !child->isVisible())
                continue;
            mask = mask.united(QRegion(QRect(int(child->x() + flowX + baseX),
                                             int(child->y() + flowY + baseY),
                                             int(child->width()),
                                             int(child->height()))));
        }
    }

    // 浮窗区域加入 mask（非浮窗模式容器不可见，不会扩大可交互区域）
    QQuickItem *floatingContainer =
        root->findChild<QQuickItem *>(QStringLiteral("floatingWidgetContainer"));
    if (floatingContainer && floatingContainer->isVisible()) {
        const qreal scale = floatingContainer->property("scale").toReal();
        const int fwWidth = int(floatingContainer->width() * scale);
        const int fwHeight = int(floatingContainer->height() * scale);
        if (fwWidth > 0 && fwHeight > 0) {
            mask = mask.united(QRegion(QRect(int(floatingContainer->x()),
                                             int(floatingContainer->y()),
                                             fwWidth, fwHeight)));
        }
    }

    m_interactiveRect = mask;
    if (mask.isEmpty()) {
        // setMask(QRegion()) 会清掉原生 mask —— 在小组件拿到有效几何之前，
        // 保留一个最小非空 mask（core.py:274-280 的 1×1 兜底，必须照搬）
        mask = QRegion(QRect(0, 0, 1, 1));
    }
    root->setMask(mask);
}

void WidgetsWindow::updateMouseState()
{
    if (m_interactiveRect.isEmpty())
        return; // 没有 mask 就不处理
    if (!m_hoverFade)
        return; // 配置关闭了悬停淡出

    QQuickWindow *root = qobject_cast<QQuickWindow *>(rootWindow());
    if (!root)
        return;

    const QPoint localPos = root->mapFromGlobal(QCursor::pos());
    const bool inMask = m_interactiveRect.contains(localPos);

    if (inMask && !m_acceptsInput) {
        root->setProperty("mouseHovered", true);
        m_acceptsInput = true;
    } else if (!inMask && m_acceptsInput) {
        root->setProperty("mouseHovered", false);
        m_acceptsInput = false;
    }
}
