#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QUrl>

class QWindow;

// 对应 pip 包 RinUI 的 RinUIWindow —— 风险 C 的落点（T9）。
// 上游全部窗口类继承它；M1 反查出的使用成员只有 11 个（见 M1 拆分文档 §1.1）。
//
// 引擎所有权双模式（C1 共享引擎，CWNext-内存优化计划.md §6）：
// - 自有模式（旧 shared_engine=False 语义）：构造时 new 独立引擎，release() 时
//   全套销毁 —— 主窗口，以及主窗口尚不存在的场景（首跑教程门、极早的主题错误
//   弹窗）仍走此路径；
// - 注入模式（C1）：传入主窗口引擎，release() 只销毁本窗口的 root 对象树，
//   保留引擎的类型缓存与 JS 堆 —— 消除旧模式"每开一个辅助窗口重编译整套
//   RinUI、关闭销毁引擎留堆碎片"的残余（Step 0 C2 实测：开窗提交 +40MB、
//   关闭残余 +31MB 不回落）。
class RinUiWindowBase : public QObject
{
    Q_OBJECT
public:
    explicit RinUiWindowBase(QObject *parent = nullptr);
    // C1 注入构造：sharedEngine 须已完成 import path / 上下文属性 / URL 拦截器
    // 注册（主窗口引擎由 WidgetsWindow::run → AppCentral::setupQmlContext 完成）
    RinUiWindowBase(QQmlApplicationEngine *sharedEngine, QObject *parent = nullptr);
    ~RinUiWindowBase() override;

    QQmlApplicationEngine *engine() const { return m_engine.data(); }
    QWindow *rootWindow() const;
    QList<QWindow *> windows() const;
    bool isReleased() const { return m_released; }

    // 对应 windows.py Tutorial 的 setTheme(Theme.Auto)；RinUI 枚举占位（0 = Auto）
    void setTheme(int autoOrTheme);

    // 加载 QML（对应 RinUIWindow.load）；上下文注册必须在调用前完成
    void load(const QUrl &qmlUrl);

    // 对应 ReleasableWindow.release()：hide → releaseResources → 销毁 root
    //（自有模式再销毁引擎；注入模式保留引擎与类型缓存）
    virtual void release();

signals:
    void qmlReady();

protected:
    virtual void cleanupEngine();

    QPointer<QQmlApplicationEngine> m_engine;
    bool m_ownsEngine = false;
    // 本窗口 load() 创建的 root 对象（QPointer 防引擎先行销毁后的悬垂）。
    // 共享引擎下绝不能遍历 engine->rootObjects() —— 那会把主窗口一起
    // hide / deleteLater。
    QList<QPointer<QObject>> m_ownRoots;
    bool m_released = false;
};
