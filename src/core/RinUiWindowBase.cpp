#include "RinUiWindowBase.h"

#include "AppPaths.h"
#include "Logger.h"

#include <QCoreApplication>
#include <QQuickWindow>

#include <utility>

RinUiWindowBase::RinUiWindowBase(QObject *parent)
    : QObject(parent)
    , m_ownsEngine(true)
{
    m_engine = new QQmlApplicationEngine();

    // 对应 central.py:409 的导入路径优先级（风险 C，必须复刻）：
    // src/qml 必须最先 —— src/qml/RinUI/components/Navigation/NavigationSubItem.qml
    // 是作者对 RinUI 的局部覆盖补丁，路径顺序决定它能否生效。
    // （部署侧同时已把该补丁覆盖进 vendored RinUI 副本，双保险。）
    // 注入模式下跳过：共享引擎在主窗口构造时已按同一次序注册过。
    const AppPaths &paths = AppPaths::instance();
    m_engine->addImportPath(paths.qmlRoot());  // ① src/qml（优先级最高）
    m_engine->addImportPath(paths.cwRoot());   // ② 上游 WidgetsWindow 亦注册 CW_PATH
    m_engine->addImportPath(paths.rinUiRoot()); // ③ vendored RinUI 兜底
    // ④ Qt 官方 QML 模块（windeployqt 部署在 exe 旁的 qml/ 目录）
    m_engine->addImportPath(QCoreApplication::applicationDirPath() + QStringLiteral("/qml"));

    connect(m_engine.data(), &QQmlApplicationEngine::warnings, this,
            [](const QList<QQmlError> &warnings) {
                cwn::Log::reportQmlWarnings(warnings);
            });
}

RinUiWindowBase::RinUiWindowBase(QQmlApplicationEngine *sharedEngine, QObject *parent)
    : QObject(parent)
    , m_ownsEngine(false)
{
    m_engine = sharedEngine;
    // import path / 上下文属性 / URL 拦截器均已由主窗口引擎完成，此处不得重复
    // 注册（重复 addUrlInterceptor 会让同一 URL 被拦截两次）；QML 告警日志也
    // 复用主引擎上已有的 warnings 连接。
}

RinUiWindowBase::~RinUiWindowBase()
{
    if (!m_released)
        release();
}

QWindow *RinUiWindowBase::rootWindow() const
{
    for (const QPointer<QObject> &obj : m_ownRoots) {
        if (QWindow *window = qobject_cast<QWindow *>(obj.data()))
            return window;
    }
    return nullptr;
}

QList<QWindow *> RinUiWindowBase::windows() const
{
    QList<QWindow *> result;
    for (const QPointer<QObject> &obj : m_ownRoots) {
        if (QWindow *window = qobject_cast<QWindow *>(obj.data()))
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
    // 本地路径同步加载；rootObjects() 前后差集即本窗口自己的 root ——
    // 共享引擎里还驻留主窗口等其他 root，绝不能全量取用
    const QList<QObject *> before = m_engine->rootObjects();
    m_engine->load(qmlUrl);
    const QList<QObject *> after = m_engine->rootObjects();
    for (QObject *obj : after) {
        if (!before.contains(obj))
            m_ownRoots.append(QPointer<QObject>(obj));
    }
}

void RinUiWindowBase::release()
{
    if (m_released)
        return;
    m_released = true;

    // 对应 ReleasableWindow.release：root hide → releaseResources → deleteLater
    // 只作用于本窗口自己的 root（注入模式下引擎里还驻留主窗口，不能全遍历）
    for (const QPointer<QObject> &obj : std::as_const(m_ownRoots)) {
        if (!obj)
            continue; // 引擎先行销毁时 QPointer 已空
        if (QQuickWindow *window = qobject_cast<QQuickWindow *>(obj.data())) {
            window->hide();
            window->releaseResources();
        }
        obj->deleteLater();
    }
    m_ownRoots.clear();

    // 注入模式到此为止：引擎归主窗口所有，类型缓存与 JS 堆保留供下次开窗复用
    if (m_ownsEngine)
        cleanupEngine();
}

void RinUiWindowBase::cleanupEngine()
{
    if (!m_engine)
        return;
    // 对应 _cleanup_engine：clearComponentCache + collectGarbage + deleteLater
    //（仅自有模式可达；注入模式绝不能 clearComponentCache —— 那会清空主引擎
    // 的类型缓存，迫使主界面全量重编译）
    m_engine->clearComponentCache();
    m_engine->collectGarbage();
    // 交出所有权交给事件循环销毁，避免与本对象析构竞争
    QQmlApplicationEngine *engine = m_engine.data();
    m_engine.clear();
    engine->deleteLater();
}
