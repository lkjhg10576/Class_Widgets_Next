#pragma once

#include <QObject>
#include <QTimer>

// 对应上游 core/timer/union_update.py UnionUpdateTimer（26 行）。
// 全局秒级心跳：所有需要按秒刷新的对象（ScheduleRuntime 等）都连接 tick()，
// 禁止各类自行再开秒级 QTimer（对应上游 central.py:456 的装配方式）。
//
// 上游实现是 50ms 轮询并检测秒变化；这里改为"对齐到下一个整秒边界 + 1000ms
// 周期"，对外的可观察语义完全一致：每秒触发一次 tick。
class UnionTimer : public QObject
{
    Q_OBJECT
public:
    static UnionTimer &instance();

    // 对应 union_update.py start()：先对齐整秒，再进入 1000ms 周期
    void start();
    void stop();

signals:
    void tick(); // 每秒触发一次

private:
    explicit UnionTimer(QObject *parent = nullptr);

    QTimer m_timer;
    bool m_started = false;
};
