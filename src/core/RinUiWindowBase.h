#pragma once

#include <QList>
#include <memory>
#include <QObject>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QUrl>

class AppCentral;

// 对应 pip 包 RinUI 的 RinUIWindow —— 风险 C 的落点（T9）。
// 上游全部窗口类继承它；M1 反查出的使用成员只有 11 个（见 M1 拆分文档 §1.1），
// 按 shared_engine=False 语义做成每窗口独立引擎。
//
// ⚠️ engine 设计为可注入（M1 拆分文档 §6.4）：M3 改共享引擎时传入句柄即可。
class RinUiWindowBase : public QObject
{
    Q_OBJECT
public:
    explicit RinUiWindowBase(QObject *parent = nullptr);
    ~RinUiWindowBase() override;

    QQmlApplicationEngine *engine() const { return m_engine.get(); }
    QWindow *rootWindow() const;
    QList<QWindow *> windows() const;
    bool isReleased() const { return m_released; }

    // 对应 windows.py Tutorial 的 setTheme(Theme.Auto)；RinUI 枚举占位（0 = Auto）
    void setTheme(int autoOrTheme);

    // 加载 QML（对应 RinUIWindow.load）；上下文注册必须在调用前完成
    void load(const QUrl &qmlUrl);

    // 对应 ReleasableWindow.release()：hide → releaseResources → 清缓存 → 销毁引擎
    virtual void release();

signals:
    void qmlReady();

protected:
    virtual void cleanupEngine();

    std::unique_ptr<QQmlApplicationEngine> m_engine;
    bool m_released = false;
};
