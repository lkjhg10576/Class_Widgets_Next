#include "RinUiWindowBase.h"

#include "AppPaths.h"
#include "Logger.h"

#include <QCoreApplication>
#include <QQuickWindow>

#include <memory>

RinUiWindowBase::RinUiWindowBase(QObject *parent)
    : QObject(parent)
{
    m_engine = std::make_unique<QQmlApplicationEngine>();

    // 对应 central.py:409 的导入路径优先级（风险 C，必须复刻）：
    // src/qml 必须最先 —— src/qml/RinUI/components/Navigation/NavigationSubItem.qml
    // 是作者对 RinUI 的局部覆盖补丁，路径顺序决定它能否生效。
    // （部署侧同时已把该补丁覆盖进 vendored RinUI 副本，双保险。）
    const AppPaths &paths = AppPaths::instance();
    m_engine->addImportPath(paths.qmlRoot());  // ① src/qml（优先级最高）
    m_engine->addImportPath(paths.cwRoot());   // ② 上游 WidgetsWindow 亦注册 CW_PATH
    m_engine->addImportPath(paths.rinUiRoot()); // ③ vendored RinUI 兜底
    // ④ Qt 官方 QML 模块（windeployqt 部署在 exe 旁的 qml/ 目录）
    m_engine->addImportPath(QCoreApplication::applicationDirPath() + QStringLiteral("/qml"));

    connect(m_engine.get(), &QQmlApplicationEngine::warnings, this,
            [](const QList<QQmlError> &warnings) {
                cwn::Log::reportQmlWarnings(warnings);
            });
}

RinUiWindowBase::~RinUiWindowBase()
{
    if (!m_released)
        release();
}

QWindow *RinUiWindowBase::rootWindow() const
{
    if (!m_engine || m_engine->rootObjects().isEmpty())
        return nullptr;
    return qobject_cast<QWindow *>(m_engine->rootObjects().constFirst());
}

QList<QWindow *> RinUiWindowBase::windows() const
{
    QList<QWindow *> result;
    if (!m_engine)
        return result;
    for (QObject *obj : m_engine->rootObjects()) {
        if (QWindow *window = qobject_cast<QWindow *>(obj))
            result.append(window);
    }
    return result;
}

void RinUiWindowBase::setTheme(int autoOrTheme)
{
    // RinUI ThemeManager 的进程级单例语义在 M3 落地；M1 仅记录
    Q_UNUSED(autoOrTheme);
    cwn::Log::info(QStringLiteral("RinUiWindowBase::setTheme(%1) (placeholder, M3)")
                       .arg(autoOrTheme));
}

void RinUiWindowBase::load(const QUrl &qmlUrl)
{
    if (m_released || !m_engine)
        return;
    cwn::Log::info(QStringLiteral("Loading QML: %1").arg(qmlUrl.toString()));
    m_engine->load(qmlUrl);
}

void RinUiWindowBase::release()
{
    if (m_released)
        return;
    m_released = true;

    // 对应 ReleasableWindow.release：root hide → releaseResources → deleteLater
    if (m_engine) {
        const QList<QObject *> roots = m_engine->rootObjects();
        for (QObject *obj : roots) {
            if (QQuickWindow *window = qobject_cast<QQuickWindow *>(obj)) {
                window->hide();
                window->releaseResources();
            }
            obj->deleteLater();
        }
    }
    cleanupEngine();
}

void RinUiWindowBase::cleanupEngine()
{
    if (!m_engine)
        return;
    // 对应 _cleanup_engine：clearComponentCache + collectGarbage + deleteLater
    m_engine->clearComponentCache();
    m_engine->collectGarbage();
    // 交出所有权交给事件循环销毁，避免与本对象析构竞争
    QQmlApplicationEngine *engine = m_engine.release();
    engine->deleteLater();
}
