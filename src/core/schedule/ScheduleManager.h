#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVariantList>

class ConfigStore;
class ScheduleIO;

// 对应上游 core/schedule/manager.py ScheduleManager（327 行）。
// 课表文件的加载/切换/保存与增删改查；课表文件存放于
// <configs>/schedules/<名称>.json（directories.py:15 SCHEDULES_PATH）。
// 课表数据用 QJsonObject 承载（见 ScheduleModel.h 的形状说明）。
class ScheduleManager : public QObject
{
    Q_OBJECT
    // manager.py:137-139（notify=scheduleSwitched）
    Q_PROPERTY(QString currentScheduleName READ currentScheduleName NOTIFY scheduleSwitched)
    // manager.py:53-55 scheduleIO（上游 notify=initialized；对象构造后不变，用 CONSTANT）
    Q_PROPERTY(QObject *scheduleIO READ scheduleIO CONSTANT)

public:
    // 对应 manager.py:38 __init__(schedules_dir, app_central)：
    // schedulesDir 为空时取 AppPaths::configsRoot()/schedules。
    explicit ScheduleManager(ConfigStore *configs, const QString &schedulesDir = QString(),
                             QObject *parent = nullptr);

    // C++ 侧访问器
    ConfigStore *configs() const { return m_configs; }
    const QJsonObject &schedule() const { return m_schedule; }
    QString schedulePath() const { return m_schedulePath; }
    // manager.schedule_path.stem（编辑器 filename 属性用）
    QString schedulePathStem() const;
    ScheduleIO *scheduleIO() const { return m_scheduleIO; }
    // manager.py:137-139 currentScheduleName（current_schedule_name or ""）
    QString currentScheduleName() const { return m_currentScheduleName; }
    bool readonly() const { return m_readonly; }
    void setReadonly(bool readonly); // manager.py:319-322 set_readonly

    // ── QML 契约（manager.py 的 @Slot，名称逐字一致）────────────

    // manager.py:57-93 load(name, force=False)：加载课表；同时写配置键
    // schedule.current_schedule。文件缺失 → 新建空表；解析失败 → 备份后新建。
    Q_INVOKABLE bool load(const QString &name, bool force = false);
    // manager.py:95-100 reload：强制重载当前课表
    Q_INVOKABLE bool reload();
    // manager.py:124-135 save(path=None)：保存当前课表到 schedule_path
    Q_INVOKABLE bool save();
    // manager.py:154-169 add：创建新空课表
    Q_INVOKABLE bool add(const QString &name);
    // manager.py:171-186 delete —— "delete" 是 C++ 关键字，无法作为方法名
    // （moc 生成代码会引用关键字标识符，无法绕过，参见 QTBUG-5426）。
    // QML ScheduleClip.qml:287 调用 scheduleManager.delete(filename) 将无法
    // 命中，属已知未覆盖项；行为与上游一致的入口是 removeSchedule。
    Q_INVOKABLE bool removeSchedule(const QString &name);
    // manager.py:188-197 duplicate：复制课表文件
    Q_INVOKABLE bool duplicate(const QString &srcName, const QString &destName);
    // manager.py:199-230 rename：重命名；若是当前课表则同步更新记录与配置
    Q_INVOKABLE bool rename(const QString &oldName, const QString &newName);
    // manager.py:232-270 importSchedule：文件对话框导入 CW2 JSON
    Q_INVOKABLE bool importSchedule();
    // manager.py:272-299 export —— "export" 同样是 C++ 关键字（C++20 模块），
    // 无法作为方法名；QML ScheduleClip.qml:146 的 scheduleManager.export(filename)
    // 属已知未覆盖项，C++ 侧入口为 exportSchedule。
    Q_INVOKABLE bool exportSchedule(const QString &filename);
    // manager.py:301-304 checkNameExists
    Q_INVOKABLE bool checkNameExists(const QString &name) const;
    // manager.py:306-317 openSchedulesFolder
    Q_INVOKABLE bool openSchedulesFolder();
    // manager.py:324-327 isReadonly（QML 以函数形式调用）
    Q_INVOKABLE bool isReadonly() const { return m_readonly; }
    // manager.py:142-152 schedules：列出目录下全部课表 → [{name, path, type:"local"}]
    Q_INVOKABLE QVariantList schedules() const;

    // manager.py:102-121 modify / modify_by_dict：接受外部修改（编辑器、换课）。
    // modifyByDict 失败时返回 false（对应 model_validate 抛错路径）。
    bool modify(const QJsonObject &schedule);
    bool modifyByDict(const QJsonObject &scheduleDict);

signals:
    void initialized(); // manager.py:34
    void scheduleSwitched(const QJsonObject &schedule); // manager.py:35
    void scheduleModified(const QJsonObject &schedule); // manager.py:36

private:
    // manager.py:129-131 save 的带路径内部形式（备份用）
    bool saveTo(const QString &path);
    QString scheduleFileOf(const QString &name) const;

    ConfigStore *m_configs = nullptr;
    ScheduleIO *m_scheduleIO = nullptr;
    QString m_schedulesDir;
    QString m_schedulePath;
    QJsonObject m_schedule;      // 当前课表（归一化后的 JSON 对象）
    QString m_currentScheduleName;
    bool m_readonly = false;     // manager.py:49
};
