#include "ScheduleIO.h"

#include "../convertor/ConvertorBridge.h"
#include "ScheduleManager.h"

// 上游 core/convertor/slots.py 的 QML 桥。三个方法经 Q_INVOKABLE 暴露给
// QML（AppCentral.scheduleManager.scheduleIO.*），实际逻辑委托给
// ConvertorBridge（slots.py 实现本体）→ ScheduleConverter（converter.py）。
ScheduleIO::ScheduleIO(ScheduleManager *manager, QObject *parent)
    : QObject(parent)
    , m_manager(manager)
    , m_bridge(new ConvertorBridge(manager, this))
{
}

bool ScheduleIO::exportToCSES(const QString &filename)
{
    // slots.py:16-35
    return m_bridge->exportToCSES(filename);
}

bool ScheduleIO::importCSES()
{
    // slots.py:37-76
    return m_bridge->importCSES();
}

bool ScheduleIO::importCW1()
{
    // slots.py:78-115
    return m_bridge->importCW1();
}
