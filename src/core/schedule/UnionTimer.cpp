#include "UnionTimer.h"

#include <QTime>

namespace {
// 距离下一个整秒边界的毫秒数（对齐用，替代上游 50ms 轮询的检测逻辑）
int millisToNextSecond()
{
    return 1000 - QTime::currentTime().msec();
}
} // namespace

UnionTimer::UnionTimer(QObject *parent)
    : QObject(parent)
{
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &UnionTimer::tick);
}

UnionTimer &UnionTimer::instance()
{
    static UnionTimer timer;
    return timer;
}

void UnionTimer::start()
{
    if (m_started) {
        return;
    }
    m_started = true;
    // 先延时对齐到整秒边界再启动周期定时器，保证 tick 与墙上时钟秒对齐
    QTimer::singleShot(millisToNextSecond(), this, [this]() {
        if (m_started) {
            m_timer.start();
        }
    });
}

void UnionTimer::stop()
{
    m_started = false;
    m_timer.stop();
}
