#include "ScheduleIO.h"

#include "../Logger.h"
#include "ScheduleManager.h"

ScheduleIO::ScheduleIO(ScheduleManager *manager, QObject *parent)
    : QObject(parent)
    , m_manager(manager)
{
}

bool ScheduleIO::exportToCSES(const QString &filename)
{
    // slots.py:12-32；实现待移植 core/convertor/converter.py（含 YAML）
    Q_UNUSED(filename);
    cwn::Log::warn(QStringLiteral("ScheduleIO::exportToCSES: converter (core/convertor/"
                                  "converter.py) is not ported yet; export skipped."));
    return false;
}

bool ScheduleIO::importCSES()
{
    // slots.py:34-69
    cwn::Log::warn(QStringLiteral("ScheduleIO::importCSES: converter (core/convertor/"
                                  "converter.py) is not ported yet; import skipped."));
    return false;
}

bool ScheduleIO::importCW1()
{
    // slots.py:71-110
    cwn::Log::warn(QStringLiteral("ScheduleIO::importCW1: converter (core/convertor/"
                                  "converter.py) is not ported yet; import skipped."));
    return false;
}
