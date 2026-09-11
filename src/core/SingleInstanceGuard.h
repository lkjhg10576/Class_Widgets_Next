#pragma once

class QLockFile;

// 对应上游 core/utils/instance_locker.py 的 SingleInstanceGuard（T12）。
// 上游实现即 QLockFile（%TEMP%/ClassWidgets2.lock），非 QLocalServer；
// 移植保持同一机制，锁名按新名调整。
class SingleInstanceGuard
{
public:
    SingleInstanceGuard();
    ~SingleInstanceGuard();

    // 尝试获取单实例锁；false 表示已有实例在运行
    bool tryAcquire(int timeoutMs = 100);
    void release();

private:
    QLockFile *m_lock = nullptr;
};
