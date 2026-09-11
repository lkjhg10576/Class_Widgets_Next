#include "SingleInstanceGuard.h"

#include <QLockFile>
#include <QDir>

SingleInstanceGuard::SingleInstanceGuard()
{
    const QString lockPath = QDir::temp().absoluteFilePath(
        QStringLiteral("ClassWidgetsNext.lock"));
    m_lock = new QLockFile(lockPath);
}

SingleInstanceGuard::~SingleInstanceGuard()
{
    release();
    delete m_lock;
}

bool SingleInstanceGuard::tryAcquire(int timeoutMs)
{
    if (!m_lock)
        return false;
    return m_lock->tryLock(timeoutMs);
}

void SingleInstanceGuard::release()
{
    if (m_lock && m_lock->isLocked())
        m_lock->unlock();
}
