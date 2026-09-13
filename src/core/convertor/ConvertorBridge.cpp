#include "ConvertorBridge.h"

#include "../AppPaths.h"
#include "../Logger.h"
#include "../schedule/ScheduleManager.h"
#include "../NativeFileDialog.h" // B4：原生文件对话框替代 QFileDialog（去 Qt6::Widgets）
#include "ScheduleConverter.h"

#include <QCoreApplication>
#include <QFileInfo>

namespace {

// 上游 slots.py 直接读 manager.schedules_dir；C++ 的 ScheduleManager 未暴露
// 该目录的访问器。课表路径始终位于 schedules 目录内（ScheduleManager 的
// load/importSchedule 都把 schedule_path 指向 scheduleFileOf(name)），因此由
// 当前课表路径推导目录；尚未加载时退回默认目录（directories.py:15
// SCHEDULES_PATH = CONFIGS_PATH / "schedules"，与 ScheduleManager.cpp:53 一致）。
QString schedulesDirOf(ScheduleManager *manager)
{
    const QString schedulePath = manager ? manager->schedulePath() : QString();
    if (!schedulePath.isEmpty())
        return QFileInfo(schedulePath).absolutePath();
    return AppPaths::instance().configsRoot() + QStringLiteral("/schedules");
}

} // namespace

ConvertorBridge::ConvertorBridge(ScheduleManager *manager, QObject *parent)
    : QObject(parent)
    , m_manager(manager)
{
}

bool ConvertorBridge::exportToCSES(const QString &filename)
{
    // slots.py:16-35 exportToCSES(filename)：导出当前课表为 CSES YAML。
    // 注意上游导出的是按名读取的课表文件，而非内存中的当前课表。
    if (!m_manager)
        return false;
    const QString schedulesDir = schedulesDirOf(m_manager);
    const QString sourcePath =
        schedulesDir + QLatin1Char('/') + filename + QStringLiteral(".json"); // slots.py:20
    const QString defaultName =
        QFileInfo(sourcePath).completeBaseName() + QStringLiteral(".yaml");   // slots.py:21 path.stem

    const QString outputPath = NativeFileDialog::getSaveFileName(
        QCoreApplication::translate("ExportScheduleDialog", "Export Schedule"),
        defaultName,
        QCoreApplication::translate("ExportScheduleDialog", "CSES Format (*.yaml *.yml)"));
    if (outputPath.isEmpty())
        return false; // slots.py:28-29：用户取消（QML 走失败分支，与上游一致）

    QString error;
    if (!ScheduleConverter::exportToCsesFile(sourcePath, outputPath, &error)) {
        cwn::Log::error(QStringLiteral("Export failed: %1").arg(error)); // slots.py:33-35
        return false;
    }
    cwn::Log::info(QStringLiteral("Exported schedule to %1").arg(outputPath)); // slots.py:31
    return true;
}

bool ConvertorBridge::importCSES()
{
    // slots.py:37-76 importCSES
    return importAndApply(
        QStringLiteral("cses"),
        QCoreApplication::translate("ImportScheduleDialog", "Import CSES Schedule"),
        QCoreApplication::translate("ImportScheduleDialog",
                                    "CSES YAML Files (*.yaml *.yml)"),
        QStringLiteral(" - CSES")); // slots.py:57 f"{path.stem} - CSES.json"
}

bool ConvertorBridge::importCW1()
{
    // slots.py:78-115 importCW1
    return importAndApply(
        QStringLiteral("cw1"),
        QCoreApplication::translate("ImportScheduleDialog",
                                    "Import Class Widgets 1 Schedule"),
        QCoreApplication::translate("ImportScheduleDialog",
                                    "Class Widgets 1 JSON Files (*.json)"),
        QStringLiteral(" - CW1")); // slots.py:97 f"{path.stem} - CW1.json"
}

bool ConvertorBridge::importAndApply(const QString &sourceFormat, const QString &dialogTitle,
                                     const QString &filter, const QString &destSuffix)
{
    if (!m_manager)
        return false;
    const QString schedulesDir = schedulesDirOf(m_manager);

    const QString filePath =
        NativeFileDialog::getOpenFileName(dialogTitle, schedulesDir, filter);
    if (filePath.isEmpty()) {
        cwn::Log::info(QStringLiteral("User cancelled import.")); // slots.py:47 / 88
        return false;
    }
    if (!QFileInfo::exists(filePath)) {
        cwn::Log::error(QStringLiteral("Selected file does not exist: %1").arg(filePath));
        return false; // slots.py:52-54 / 93-95
    }

    const QFileInfo info(filePath);
    const QString destPath = schedulesDir + QLatin1Char('/') + info.completeBaseName()
                             + destSuffix + QStringLiteral(".json");
    QString error;
    if (!ScheduleConverter::convertToCw2File(filePath, sourceFormat, destPath, &error)) {
        cwn::Log::error(QStringLiteral("Import failed: %1").arg(error)); // slots.py:74-75 / 113-114
        return false;
    }

    // slots.py:60-70：转换后把结果应用为当前课表并触发信号。C++ 的
    // ScheduleManager 未公开 schedule / current_schedule_name / schedule_path
    // 的写入口，等价实现为加载刚写入的目标文件：load() 会设置三项状态并
    // 先后发出 scheduleSwitched、scheduleModified（与 slots.py:69-70 的顺序
    // 一致）。与上游的偏差：load() 会写配置键 schedule.current_schedule，
    // 而上游 import 只改运行时字段、不写配置（重启后打开的是导入的课表而
    // 非上一个课表）。
    const QString destName = info.completeBaseName() + destSuffix;
    if (!m_manager->load(destName, /*force=*/true)) {
        cwn::Log::error(QStringLiteral("Import failed: cannot load converted schedule %1")
                            .arg(destPath));
        return false;
    }
    cwn::Log::info(QStringLiteral("Imported %1 schedule from %2")
                       .arg(sourceFormat == QLatin1String("cw1") ? QStringLiteral("CW1")
                                                                 : QStringLiteral("CSES"),
                            filePath)); // slots.py:72 / 111
    return true;
}
