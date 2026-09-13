#include "ScheduleManager.h"

#include "../AppPaths.h"
#include "../ConfigStore.h"
#include "../Logger.h"
#include "../NativeFileDialog.h" // B4：原生文件对话框替代 QFileDialog（去 Qt6::Widgets）
#include "ScheduleIO.h"
#include "ScheduleModel.h"
#include "ScheduleParser.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QUrl>
#include <QDesktopServices>

using namespace ScheduleModel;

namespace {
// 备份目录：<schedules>/backup（上游 manager.py:83 的备份路径拼接有误，
// 实际从不生效；这里改为可用的等价实现，语义：解析失败时保留原文件内容）
QString backupDirOf(const QString &schedulesDir)
{
    return schedulesDir + QStringLiteral("/backup");
}

bool writeJsonFile(const QString &path, const QJsonObject &schedule)
{
    // manager.py:129-131：json.dump(..., ensure_ascii=False, indent=4)
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        cwn::Log::error(QStringLiteral("ScheduleManager: cannot write %1: %2")
                            .arg(path, file.errorString()));
        return false;
    }
    const QJsonDocument doc(schedule);
    file.write(doc.toJson(QJsonDocument::Indented));
    return true;
}
} // namespace

ScheduleManager::ScheduleManager(ConfigStore *configs, const QString &schedulesDir,
                                 QObject *parent)
    : QObject(parent)
    , m_configs(configs)
{
    m_schedulesDir = schedulesDir;
    if (m_schedulesDir.isEmpty()) {
        // directories.py:15 SCHEDULES_PATH = CONFIGS_PATH / "schedules"
        m_schedulesDir = AppPaths::instance().configsRoot() + QStringLiteral("/schedules");
    }
    QDir().mkpath(m_schedulesDir);

    m_scheduleIO = new ScheduleIO(this, this);
    m_schedule = makeEmptySchedule(); // manager.py:46 初始为空课表
    emit initialized();               // manager.py:51
}

QString ScheduleManager::schedulePathStem() const
{
    return QFileInfo(m_schedulePath).completeBaseName();
}

void ScheduleManager::setReadonly(bool readonly)
{
    // manager.py:319-322
    m_readonly = readonly;
    cwn::Log::info(QStringLiteral("Schedule read-only mode set to: %1").arg(readonly));
}

QString ScheduleManager::scheduleFileOf(const QString &name) const
{
    return m_schedulesDir + QLatin1Char('/') + name + QStringLiteral(".json");
}

bool ScheduleManager::load(const QString &name, bool force)
{
    // manager.py:57-70：锁定检查 + 名称短路
    if (m_configs && m_configs->isKeyLocked(QStringLiteral("schedule.current_schedule"))) {
        cwn::Log::warn(QStringLiteral("Attempt to modify locked config key: "
                                      "schedule.current_schedule. Blocked."));
        return false;
    }
    if (name == m_currentScheduleName && !force) {
        return true;
    }

    cwn::Log::info(QStringLiteral("Loading schedule: %1").arg(name));
    const QString path = scheduleFileOf(name);
    m_schedulePath = path;
    m_currentScheduleName = name;
    if (m_configs) {
        m_configs->set(QStringLiteral("schedule.current_schedule"), name);
    }

    ScheduleParser parser(path);
    QJsonObject loaded;
    QString error;
    const ScheduleParser::Status status = parser.load(&loaded, &error);
    if (status == ScheduleParser::Status::Ok) {
        m_schedule = loaded;
        cwn::Log::info(QStringLiteral("Schedule loaded from %1").arg(m_schedulePath));
    } else if (status == ScheduleParser::Status::FileNotFound) {
        // manager.py:76-79：文件不存在则新建空课表
        cwn::Log::warn(QStringLiteral("Schedule file not found, creating a new one..."));
        m_schedule = makeEmptySchedule();
        saveTo(m_schedulePath);
    } else {
        // manager.py:80-89：解析失败 → 备份 → 新建空课表 → 返回 false。
        // 上游备份写入的其实是"内存中的旧课表"（save(backup_path)），此处保持
        // 同一语义但落到可用的备份目录。
        cwn::Log::error(QStringLiteral("Failed to load schedule: %1").arg(error));
        if (QFileInfo::exists(path)) {
            QDir().mkpath(backupDirOf(m_schedulesDir));
            const QString backupPath = backupDirOf(m_schedulesDir) + QLatin1Char('/')
                + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmsss"))
                + QStringLiteral(".json");
            if (saveTo(backupPath)) {
                cwn::Log::info(QStringLiteral("Original schedule backed up to %1")
                                   .arg(QFileInfo(backupPath).fileName()));
            }
        }
        m_schedule = makeEmptySchedule();
        saveTo(m_schedulePath);
        return false;
    }

    const QJsonObject schedule = m_schedule;
    emit scheduleSwitched(schedule);   // manager.py:91
    emit scheduleModified(schedule);   // manager.py:92
    return true;
}

bool ScheduleManager::reload()
{
    // manager.py:95-100
    if (m_currentScheduleName.isEmpty()) {
        return false;
    }
    return load(m_currentScheduleName, /*force=*/true);
}

bool ScheduleManager::saveTo(const QString &path)
{
    return writeJsonFile(path, m_schedule);
}

bool ScheduleManager::save()
{
    // manager.py:124-135
    return saveTo(m_schedulePath);
}

bool ScheduleManager::modify(const QJsonObject &schedule)
{
    // manager.py:102-113 modify：接受外部修改（编辑器、换课）
    if (m_readonly) {
        cwn::Log::warn(QStringLiteral("Attempt to modify schedule while in read-only "
                                      "mode. Blocked."));
        return false;
    }
    m_schedule = normalizeSchedule(schedule);
    const QJsonObject current = m_schedule;
    emit scheduleModified(current);
    return true;
}

bool ScheduleManager::modifyByDict(const QJsonObject &scheduleDict)
{
    // manager.py:115-122 modify_by_dict：非课表结构直接拒绝
    if (!ScheduleParser::validate(scheduleDict)) {
        cwn::Log::error(QStringLiteral("Failed to modify schedule: invalid schedule dict"));
        return false;
    }
    return modify(scheduleDict);
}

bool ScheduleManager::add(const QString &name)
{
    // manager.py:154-169 add：创建新空课表文件
    const QString path = scheduleFileOf(name);
    if (QFileInfo::exists(path)) {
        cwn::Log::warn(QStringLiteral("Schedule already exists: %1").arg(name));
        return false;
    }
    if (writeJsonFile(path, makeEmptySchedule())) {
        cwn::Log::info(QStringLiteral("New schedule created: %1").arg(name));
        return true;
    }
    return false;
}

bool ScheduleManager::removeSchedule(const QString &name)
{
    // manager.py:171-186 delete（QML 侧方法名为 delete，见头文件说明）
    if (name == m_currentScheduleName) {
        cwn::Log::warn(QStringLiteral("Cannot delete current schedule: %1").arg(name));
        return false;
    }
    const QString path = scheduleFileOf(name);
    if (QFile::exists(path)) {
        if (!QFile::remove(path)) {
            cwn::Log::error(QStringLiteral("Error deleting schedule: %1").arg(name));
            return false;
        }
        cwn::Log::info(QStringLiteral("Schedule deleted: %1").arg(name));
    }
    return true;
}

bool ScheduleManager::duplicate(const QString &srcName, const QString &destName)
{
    // manager.py:188-197 duplicate：shutil.copy（目标存在时覆盖）
    const QString srcPath = scheduleFileOf(srcName);
    const QString destPath = scheduleFileOf(destName);
    if (!QFileInfo::exists(srcPath)) {
        return false;
    }
    if (QFileInfo::exists(destPath)) {
        QFile::remove(destPath);
    }
    if (!QFile::copy(srcPath, destPath)) {
        return false;
    }
    cwn::Log::info(QStringLiteral("Schedule copied: %1 -> %2").arg(srcName, destName));
    return true;
}

bool ScheduleManager::rename(const QString &oldName, const QString &newName)
{
    // manager.py:199-230 rename
    if (m_configs && m_configs->isKeyLocked(QStringLiteral("schedule.current_schedule"))) {
        cwn::Log::warn(QStringLiteral("Attempt to modify locked config key: "
                                      "schedule.current_schedule. Blocked."));
        return false;
    }
    const QString oldPath = scheduleFileOf(oldName);
    const QString newPath = scheduleFileOf(newName);
    if (!QFileInfo::exists(oldPath)) {
        cwn::Log::warn(QStringLiteral("Schedule to rename does not exist: %1").arg(oldName));
        return false;
    }
    if (QFileInfo::exists(newPath)) {
        cwn::Log::warn(QStringLiteral("Target schedule name already exists: %1").arg(newName));
        return false;
    }
    if (!QFile::rename(oldPath, newPath)) {
        cwn::Log::error(QStringLiteral("Failed to rename schedule: %1 -> %2").arg(oldName, newName));
        return false;
    }
    cwn::Log::info(QStringLiteral("Schedule renamed: %1 -> %2").arg(oldName, newName));

    // manager.py:219-225：当前课表被重命名时同步运行时记录
    if (m_currentScheduleName == oldName) {
        m_currentScheduleName = newName;
        m_schedulePath = newPath;
        if (m_configs) {
            m_configs->set(QStringLiteral("schedule.current_schedule"), newName);
        }
        emit scheduleSwitched(m_schedule);
        emit scheduleModified(m_schedule);
    }
    return true;
}

bool ScheduleManager::importSchedule()
{
    // manager.py:232-270 importSchedule
    const QString filePath = NativeFileDialog::getOpenFileName(
        QCoreApplication::translate("ImportScheduleDialog", "Import Schedule"),
        m_schedulesDir,
        QCoreApplication::translate("ImportScheduleDialog",
                                    "Class Widgets Next JSON Files (*.json)"));
    if (filePath.isEmpty()) {
        cwn::Log::info(QStringLiteral("User cancelled import."));
        return false;
    }
    if (!QFileInfo::exists(filePath)) {
        cwn::Log::error(QStringLiteral("Selected file does not exist: %1").arg(filePath));
        return false;
    }

    ScheduleParser parser(filePath);
    QJsonObject imported;
    QString error;
    if (parser.load(&imported, &error) != ScheduleParser::Status::Ok) {
        cwn::Log::error(QStringLiteral("Failed to import schedule: %1").arg(error));
        return false;
    }

    m_schedule = imported;
    m_currentScheduleName = QFileInfo(filePath).completeBaseName();
    m_schedulePath = scheduleFileOf(m_currentScheduleName);
    saveTo(m_schedulePath);

    emit scheduleSwitched(m_schedule);
    emit scheduleModified(m_schedule);
    cwn::Log::info(QStringLiteral("Schedule imported from %1").arg(QFileInfo(filePath).fileName()));
    return true;
}

bool ScheduleManager::exportSchedule(const QString &filename)
{
    // manager.py:272-299 export（QML 方法名为 export，C++ 关键字限制见头文件）
    if (filename.isEmpty()) {
        cwn::Log::warn(QStringLiteral("未指定课程表名，无法导出"));
        return false;
    }
    const QString srcPath = scheduleFileOf(filename);
    if (!QFileInfo::exists(srcPath)) {
        cwn::Log::error(QStringLiteral("课程表不存在: %1").arg(filename));
        return false;
    }

    const QString filePath = NativeFileDialog::getSaveFileName(
        QCoreApplication::translate("ExportScheduleDialog", "Export Schedule"),
        filename + QStringLiteral(".json"),
        QCoreApplication::translate("ExportScheduleDialog",
                                    "Class Widgets Next JSON Files (*.json)"));
    if (filePath.isEmpty()) {
        return false; // 用户取消
    }
    if (QFileInfo::exists(filePath)) {
        QFile::remove(filePath);
    }
    if (!QFile::copy(srcPath, filePath)) {
        cwn::Log::error(QStringLiteral("Export failed: %1").arg(filename));
        return false;
    }
    cwn::Log::info(QStringLiteral("Schedule '%1' exported to: %2").arg(filename, filePath));
    return true;
}

bool ScheduleManager::checkNameExists(const QString &name) const
{
    // manager.py:301-304
    return QFileInfo::exists(scheduleFileOf(name));
}

bool ScheduleManager::openSchedulesFolder()
{
    // manager.py:306-317
    const QUrl url = QUrl::fromLocalFile(m_schedulesDir);
    const bool success = QDesktopServices::openUrl(url);
    if (!success) {
        cwn::Log::error(QStringLiteral("Failed to open plugin folder: %1").arg(m_schedulesDir));
    }
    return success;
}

QVariantList ScheduleManager::schedules() const
{
    // manager.py:142-152 schedules：列出目录下全部 *.json
    QVariantList files;
    const QFileInfoList entries = QDir(m_schedulesDir)
                                      .entryInfoList(QStringList{ QStringLiteral("*.json") },
                                                     QDir::Files, QDir::Name);
    files.reserve(entries.size());
    for (const QFileInfo &info : entries) {
        QVariantMap item;
        item.insert(QStringLiteral("name"), info.completeBaseName());
        item.insert(QStringLiteral("path"), info.absoluteFilePath());
        item.insert(QStringLiteral("type"), QStringLiteral("local")); // 未来可以做拓展
        files.append(item);
    }
    return files;
}
