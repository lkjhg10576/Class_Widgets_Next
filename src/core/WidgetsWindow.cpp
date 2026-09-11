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

WidgetsWindow::WidgetsWindow(AppCentral *central, QObject *parent)
    : RinUiWindowBase(parent)
    , m_central(central)
{
    m_mainQmlUrl = QUrl::fromLocalFile(AppPaths::instance().qmlRoot()
                                       + QStringLiteral("/MainInterface.qml"));

    // 对应 core.py:36 的 QueuedConnection
    connect(m_engine.get(), &QQmlApplicationEngine::objectCreated,
            this, &WidgetsWindow::onQmlReady, Qt::QueuedConnection);
}

void WidgetsWindow::run()
{
    // 对应 WidgetsWindow.run：注册 QML 上下文 → 加载 preset → 连主题信号 → 带主题加载主界面
    m_central->setupQmlContext(engine());
    m_central->widgetsModel()->loadConfig();

    // hover_fade 缓存：33ms 轮询里不能每帧重建整棵配置 QVariantMap
    const QVariantMap interactions =
        m_central->configs()->data().toMap().value(QStringLiteral("interactions")).toMap();
    m_hoverFade = interactions.value(QStringLiteral("hover_fade")).toBool();
    connect(m_central->configs(), &ConfigStore::dataChanged, this, [this] {
        const QVariantMap interactions =
            m_central->configs()->data().toMap().value(QStringLiteral("interactions")).toMap();
        m_hoverFade = interactions.value(QStringLiteral("hover_fade")).toBool();
    });

    // 连接要先于首次加载：无效启动主题在失败处理选择默认主题后可立即替换
    connect(m_central->themeManagerObject(), &CWThemeManager::themeChanged,
            this, &WidgetsWindow::onThemeChanged);

    load(m_mainQmlUrl);

    // 33ms 轮询（core.py:39-42；M3 改事件驱动）
    connect(&m_mouseTimer, &QTimer::timeout, this, &WidgetsWindow::updateMouseState);
    m_mouseTimer.start(33);
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
    if (menuShow || editMode) {
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
