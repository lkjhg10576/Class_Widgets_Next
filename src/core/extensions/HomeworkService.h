#pragma once

#include <QDate>
#include <QJsonArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

class ConfigStore;
class NotificationProvider;

// 当日作业扩展服务（classwidgets.ext.homework，本仓库自有模型）。
//
// 数据载体：独立按天文件 `app/configs/homework/YYYY-MM-DD.json`（仿
// configs/schedules 的 ScheduleIO 模式，不写入 configs.json 避免膨胀）。
// 单文件形状：
//   {"date":"2026-10-03",
//    "items":[{"id":"uuid","subjectId":"geo","content":"练习册P4~P6",
//              "priority":"none|orange|blue|green","updatedAt":1234567890}]}
//
// 职责：按天读写、日期过滤（itemsForDate）、启动/跨天清理过期文件
// （extensions.homework.retention_days = 1/3/7，保留最近 N 天）、
// 「作业布置」灵动通知播报（provider com.classwidgets.homework）。
// 心跳复用 UnionTimer::tick（跨天检测），不自开秒级 QTimer。
// QML 经上下文名 "Homework" 访问。
class HomeworkService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList todayItems READ todayItems NOTIFY itemsChanged)
    Q_PROPERTY(QString todayDate READ todayDate NOTIFY dateChanged)

public:
    explicit HomeworkService(ConfigStore *configs, QObject *parent = nullptr);

    QVariantList todayItems() const { return m_todayItems; }
    QString todayDate() const { return m_todayDate; }

    // ── QML 契约 ──
    // 写当天文件；content 必填（空白拒绝）。写盘成功后 reload + itemsChanged，
    // 返回是否成功（QML 侧据此决定对话框是否关闭）。
    Q_INVOKABLE bool addItem(const QString &subjectId, const QString &content,
                             const QString &priority);
    Q_INVOKABLE bool updateItem(const QString &id, const QString &subjectId,
                                const QString &content, const QString &priority);
    Q_INVOKABLE bool removeItem(const QString &id);
    // 重读当天文件（外部改动/跨天后的手工刷新入口）
    Q_INVOKABLE void reload();

    // 管理面扩展位（当前 UI 未消费，供后续历史查看复用）：
    // 指定日期 "yyyy-MM-dd" 的条目；已存盘日期列表（升序）
    Q_INVOKABLE QVariantList itemsForDate(const QString &date) const;
    Q_INVOKABLE QStringList storedDates() const;

    // 下课「作业布置」灵动通知：标题「作业布置」；subjectName 非空 →
    // 「请{subjectName}课代表填写当日作业」，空 → 「请各科课代表填写当日作业」。
    // 通知是否可见由 NotificationProvider 体系（notifications.providers.*）决定。
    Q_INVOKABLE void notify(const QString &subjectName);

signals:
    void itemsChanged();
    void dateChanged();

private:
    // 单日文件读取结果：Missing=文件不存在（正常空），Ok=已解析，Corrupt=存在
    // 但非法（解析失败/根非对象）。Corrupt 时调用方必须先备份原字节再决定覆写，
    // 否则下一次「读-改-写」会把用户当日数据静默抹掉。
    enum class DayRead { Missing, Ok, Corrupt };

    void setToday(const QDate &date);
    void onTick();
    void cleanupExpired();
    int retentionDays() const;
    // 可偏移时钟（schedule.time_offset，单位秒——与 ScheduleRuntime/通知指纹同源）：
    // 调试改期时保证作业文件日期与通知链路同天
    QDate currentDate() const;

    QString homeworkDir() const;
    QString dayFilePath(const QDate &date) const;
    DayRead readDayFile(const QDate &date, QJsonArray *items) const;
    // 写路径（add/update/remove）专用的读取：解析成功即字段级自愈（补缺失 id）；
    // 损坏则先备份到 corrupt-* 再按空处理（先留底再开新）。返回 false = 留底
    // 失败，调用方必须放弃本次写入（宁可拒写也不能把用户当日数据覆盖掉）
    bool loadDayForWrite(const QDate &date, QJsonArray *items) const;
    // 解析成功后的字段级自愈：补缺失 id、规范化字段。条目不丢（丢条目只能由
    // 显式写路径决定），返回是否发生修补。displayOnly 时剔除空正文条目。
    QJsonArray sanitizeItems(const QJsonArray &items, bool *repaired,
                             bool displayOnly) const;
    bool backupCorruptFile(const QDate &date) const;
    bool writeDayItems(const QDate &date, const QJsonArray &items);
    QVariantList toVariantItems(const QJsonArray &items) const;

    ConfigStore *m_configs = nullptr;
    NotificationProvider *m_notifyProvider = nullptr;
    QDate m_today;
    QString m_todayDate;
    QVariantList m_todayItems;
};
