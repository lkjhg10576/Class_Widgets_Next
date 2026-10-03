#include "HomeworkService.h"

#include "AppPaths.h"
#include "ConfigStore.h"
#include "Logger.h"
#include "../notification/NotificationModel.h"
#include "../notification/NotificationProvider.h"
#include "../schedule/UnionTimer.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSaveFile>
#include <QUuid>

namespace {
// 作业目录名（configs 根之下）与保留时长配置键
const char kHomeworkDirName[] = "homework";
const char kRetentionDaysKey[] = "extensions.homework.retention_days";
// 灵动通知来源 id：NotificationProvider 自动注册，设置页可单独开关
const char kNotifyProviderId[] = "com.classwidgets.homework";
const char kDatePattern[] = "yyyy-MM-dd";
// 文件名白名单：只碰本服务命名的文件，不误删目录内其他内容
const char kFilePattern[] = "????-??-??.json";

QString generateHomeworkId()
{
    // 与 ScheduleModel::generateId 同形的无括号 UUID 字符串
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString normalizePriority(const QString &priority)
{
    static const QStringList allowed = {
        QStringLiteral("none"), QStringLiteral("orange"),
        QStringLiteral("blue"), QStringLiteral("green"),
    };
    return allowed.contains(priority) ? priority : QStringLiteral("none");
}
} // namespace

HomeworkService::HomeworkService(ConfigStore *configs, QObject *parent)
    : QObject(parent)
    , m_configs(configs)
{
    // 通知来源随服务创建注册（服务生命周期 = 应用生命周期，与 RollCallService 同）
    m_notifyProvider = new NotificationProvider(
        QString::fromLatin1(kNotifyProviderId), tr("Homework"),
        QStringLiteral("ic_fluent_clipboard_task_20_regular"), false, nullptr, this);

    setToday(currentDate());       // 首次装载当天条目（内含 itemsChanged）
    cleanupExpired();              // 启动即清理超期文件
    // 心跳复用全局秒级定时器：只需跨天检测，零额外唤醒
    connect(&UnionTimer::instance(), &UnionTimer::tick, this, &HomeworkService::onTick);
    // 保留时长在设置页改为更短时立即清理一次（否则要等跨天/重启才生效，
    // 与设置页「超期自动删除」文案不符）。dataChanged 无参数，任何配置写入
    // 都会触发；cleanupExpired 只做一次目录枚举 + 日期白名单比对，开销可忽略。
    if (m_configs)
        connect(m_configs, &ConfigStore::dataChanged, this,
                &HomeworkService::cleanupExpired);
}

// ─────────────────────────── 读写 ───────────────────────────

bool HomeworkService::addItem(const QString &subjectId, const QString &content,
                              const QString &priority)
{
    const QString trimmed = content.trimmed();
    if (trimmed.isEmpty())
        return false; // 正文必填

    QJsonArray items;
    if (!loadDayForWrite(m_today, &items))
        return false; // 当日文件损坏且留底失败：拒绝写入，防覆盖丢数据
    QJsonObject item;
    item.insert(QStringLiteral("id"), generateHomeworkId());
    item.insert(QStringLiteral("subjectId"), subjectId);
    item.insert(QStringLiteral("content"), trimmed);
    item.insert(QStringLiteral("priority"), normalizePriority(priority));
    item.insert(QStringLiteral("updatedAt"),
                static_cast<double>(QDateTime::currentSecsSinceEpoch()));
    items.append(item);

    if (!writeDayItems(m_today, items))
        return false;
    reload();
    return true;
}

bool HomeworkService::updateItem(const QString &id, const QString &subjectId,
                                 const QString &content, const QString &priority)
{
    const QString trimmed = content.trimmed();
    if (id.isEmpty() || trimmed.isEmpty())
        return false;

    QJsonArray items;
    if (!loadDayForWrite(m_today, &items))
        return false; // 同 addItem：损坏且留底失败时拒绝写入
    bool found = false;
    for (int i = 0; i < items.size(); ++i) {
        QJsonObject item = items.at(i).toObject();
        if (item.value(QLatin1String("id")).toString() != id)
            continue;
        item.insert(QStringLiteral("subjectId"), subjectId);
        item.insert(QStringLiteral("content"), trimmed);
        item.insert(QStringLiteral("priority"), normalizePriority(priority));
        item.insert(QStringLiteral("updatedAt"),
                    static_cast<double>(QDateTime::currentSecsSinceEpoch()));
        items[i] = item;
        found = true;
        break;
    }
    if (!found)
        return false;

    if (!writeDayItems(m_today, items))
        return false;
    reload();
    return true;
}

bool HomeworkService::removeItem(const QString &id)
{
    if (id.isEmpty())
        return false;

    QJsonArray items;
    if (!loadDayForWrite(m_today, &items))
        return false;
    QJsonArray kept;
    bool removed = false;
    for (const QJsonValue &value : items) {
        if (value.toObject().value(QLatin1String("id")).toString() == id) {
            removed = true;
            continue;
        }
        kept.append(value);
    }
    if (!removed)
        return false;

    if (!writeDayItems(m_today, kept))
        return false;
    reload();
    return true;
}

void HomeworkService::reload()
{
    QJsonArray raw;
    const DayRead read = readDayFile(m_today, &raw);
    if (read == DayRead::Corrupt) {
        // 当天文件损坏：先留底再按空显示。reload 自身不覆写文件，留底失败时
        // 显示为空是安全的——后续任何写路径都会因留底失败被拒绝
        backupCorruptFile(m_today);
        m_todayItems = {};
        emit itemsChanged();
        return;
    }
    // 字段级自愈（补缺失 id 等）落盘回写一次，保证内存里的 id 与磁盘一致，
    // QML 拿到的 id 才能 updateItem 命中
    bool repaired = false;
    const QJsonArray cleaned = sanitizeItems(raw, &repaired, false);
    if (repaired && !writeDayItems(m_today, cleaned)) {
        cwn::Log::warn(QStringLiteral(
            "HomeworkService: repaired items for %1 could not be persisted; "
            "memory ids may differ from disk until next successful write")
            .arg(m_todayDate));
    }
    m_todayItems = toVariantItems(cleaned);
    emit itemsChanged();
}

QVariantList HomeworkService::itemsForDate(const QString &date) const
{
    const QDate parsed = QDate::fromString(date, QLatin1String(kDatePattern));
    if (!parsed.isValid())
        return {};
    QJsonArray raw;
    if (readDayFile(parsed, &raw) != DayRead::Ok)
        return {};
    bool repaired = false;
    return toVariantItems(sanitizeItems(raw, &repaired, true)); // 只读展示，不回写
}

QStringList HomeworkService::storedDates() const
{
    QStringList dates;
    const QDir dir(homeworkDir());
    const QStringList files =
        dir.entryList({ QString::fromLatin1(kFilePattern) }, QDir::Files, QDir::Name);
    for (const QString &name : files) {
        const QDate date = QDate::fromString(name.left(10), QLatin1String(kDatePattern));
        if (date.isValid())
            dates.append(date.toString(QLatin1String(kDatePattern)));
    }
    return dates; // QDir::Name 升序，等值于日期升序（零填充格式）
}

// ─────────────────────────── 通知 ───────────────────────────

void HomeworkService::notify(const QString &subjectName)
{
    if (!m_notifyProvider)
        return;
    const QString message = subjectName.isEmpty()
        ? tr("请各科课代表填写当日作业")
        : tr("请%1课代表填写当日作业").arg(subjectName);
    // LevelAnnouncement 与上下课铃/点名播报同级（动态通知组件自动覆盖显示）
    m_notifyProvider->push(cwn::notification::LevelAnnouncement, tr("作业布置"), message,
                           5000, true);
}

// ─────────────────────────── 跨天与清理 ───────────────────────────

void HomeworkService::setToday(const QDate &date)
{
    m_today = date;
    const QString dateStr = date.toString(QLatin1String(kDatePattern));
    if (dateStr != m_todayDate) {
        m_todayDate = dateStr;
        emit dateChanged();
    }
    reload();
}

void HomeworkService::onTick()
{
    const QDate today = currentDate();
    if (today == m_today)
        return;
    // 跨天：切到新的一天（重读新文件）并清一次过期；连续多天未开机也由
    // cleanupExpired 的日期算术一次性清掉全部超期文件
    setToday(today);
    cleanupExpired();
}

void HomeworkService::cleanupExpired()
{
    const QDir dir(homeworkDir());
    if (!dir.exists())
        return;

    // 保留最近 N 天：文件日期 >= 今天-(N-1) 保留，更早删除；未来日期保留
    const QDate oldest = currentDate().addDays(-(retentionDays() - 1));
    int removed = 0;
    const QStringList files =
        dir.entryList({ QString::fromLatin1(kFilePattern) }, QDir::Files);
    for (const QString &name : files) {
        const QDate date = QDate::fromString(name.left(10), QLatin1String(kDatePattern));
        if (!date.isValid())
            continue; // 非法日期文件名不碰
        if (date >= oldest)
            continue;
        if (QFile::remove(dir.filePath(name)))
            ++removed;
    }
    if (removed > 0) {
        cwn::Log::info(QStringLiteral("HomeworkService: removed %1 expired day file(s)")
                           .arg(removed));
    }
}

int HomeworkService::retentionDays() const
{
    int days = 7;
    if (m_configs) {
        if (const auto value = m_configs->value(QLatin1String(kRetentionDaysKey)))
            days = value->toInt(7);
    }
    // 白名单 1/3/7（ConfigStore::sanitize 已钳位，此处防手改内存态）
    if (days != 1 && days != 3 && days != 7)
        days = 7;
    return days;
}

// ─────────────────────────── 文件 IO ───────────────────────────

QString HomeworkService::homeworkDir() const
{
    return AppPaths::instance().configsRoot() + QLatin1Char('/')
        + QLatin1String(kHomeworkDirName);
}

QString HomeworkService::dayFilePath(const QDate &date) const
{
    return homeworkDir() + QLatin1Char('/')
        + date.toString(QLatin1String(kDatePattern)) + QStringLiteral(".json");
}

QDate HomeworkService::currentDate() const
{
    // schedule.time_offset 全仓单位是「秒」（设置页 Time.qml 标题 Seconds、
    // ScheduleRuntime.cpp:409 直接 addSecs(newOffset)）。作业文件日期对齐同一
    // 偏移时钟（通知指纹 NotificationService 也用它），避免调试改期时两边差一天。
    // 注意不要对齐 runtime.currentDate——那个属性返回的是未偏移的真实日期
    int offsetSeconds = 0;
    if (m_configs) {
        if (const auto value =
                m_configs->value(QLatin1String("schedule.time_offset")))
            offsetSeconds = value->toInt(0);
    }
    if (offsetSeconds == 0)
        return QDate::currentDate();
    return QDateTime::currentDateTime().addSecs(offsetSeconds).date();
}

HomeworkService::DayRead HomeworkService::readDayFile(const QDate &date,
                                                      QJsonArray *items) const
{
    QFile file(dayFilePath(date));
    if (!file.exists())
        return DayRead::Missing;
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        // 存在但读不开（权限/占用）：按损坏处理，调用方会留底，绝不静默清空
        cwn::Log::error(QStringLiteral("HomeworkService: cannot open %1 (%2)")
                            .arg(dayFilePath(date))
                            .arg(file.errorString()));
        return DayRead::Corrupt;
    }
    const QByteArray raw = file.readAll();
    file.close();

    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    if (!doc.isObject())
        return DayRead::Corrupt;
    // items 缺失/为 null 容忍为空日；存在但不是数组 = 结构性损坏，同样必须留底
    //（toArray() 对非数组静默返回空，直接放行会在下次写盘时覆盖掉全部数据）
    const QJsonValue itemsValue = doc.object().value(QStringLiteral("items"));
    if (!itemsValue.isUndefined() && !itemsValue.isNull() && !itemsValue.isArray())
        return DayRead::Corrupt;
    *items = itemsValue.toArray();
    return DayRead::Ok;
}

bool HomeworkService::loadDayForWrite(const QDate &date, QJsonArray *items) const
{
    QJsonArray raw;
    const DayRead read = readDayFile(date, &raw);
    if (read == DayRead::Missing) {
        *items = QJsonArray();
        return true;
    }
    if (read == DayRead::Corrupt) {
        // 留底失败 = 磁盘不可写/权限异常，此时覆写必然丢数据，必须让调用方拒写
        if (!backupCorruptFile(date))
            return false;
        cwn::Log::warn(QStringLiteral(
            "HomeworkService: day file for %1 corrupt, starting fresh "
            "(raw bytes kept in corrupt backup)")
            .arg(date.toString(QLatin1String(kDatePattern))));
        *items = QJsonArray();
        return true;
    }
    bool repaired = false;
    *items = sanitizeItems(raw, &repaired, false);
    return true;
}

QJsonArray HomeworkService::sanitizeItems(const QJsonArray &items, bool *repaired,
                                          bool displayOnly) const
{
    // 字段级防御：坏数据不崩；缺失 id 现场补齐并随下一次写盘持久化（id 稳定，
    // QML 选中/编辑目标不漂移）。displayOnly（纯展示路径）额外剔除空正文条目；
    // 写路径保留原条目（丢弃只能由用户显式增删决定，防静默截断）
    QJsonArray out;
    bool changed = false;
    for (const QJsonValue &value : items) {
        if (!value.isObject())
            continue;
        QJsonObject item = value.toObject();
        const QString originalContent = item.value(QLatin1String("content")).toString();
        const QString content = originalContent.trimmed();
        if (displayOnly && content.isEmpty())
            continue;
        if (content != originalContent)
            changed = true;
        QString id = item.value(QLatin1String("id")).toString();
        if (id.isEmpty() && !displayOnly) {
            // 只读展示路径不造 id（每次调用结果必须稳定，否则两次查询同一
            // 文件会得到不同 id）；写路径补 id 并随本次写盘持久化
            id = generateHomeworkId();
            changed = true;
        }
        item.insert(QStringLiteral("id"), id);
        item.insert(QStringLiteral("subjectId"),
                    item.value(QStringLiteral("subjectId")).toString());
        item.insert(QStringLiteral("content"), content);
        const QString priority =
            normalizePriority(item.value(QStringLiteral("priority")).toString());
        if (priority != item.value(QLatin1String("priority")).toString())
            changed = true;
        item.insert(QStringLiteral("priority"), priority);
        item.insert(QStringLiteral("updatedAt"),
                    item.value(QStringLiteral("updatedAt")).toDouble());
        out.append(item);
    }
    if (repaired)
        *repaired = changed;
    return out;
}

bool HomeworkService::backupCorruptFile(const QDate &date) const
{
    const QString source = dayFilePath(date);
    // 毫秒级时间戳：同秒内多次发现损坏（reload 后紧跟写路径）也不撞名
    const QString stamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    const QString target = homeworkDir() + QStringLiteral("/corrupt-")
        + date.toString(QLatin1String(kDatePattern)) + QStringLiteral("-") + stamp
        + QStringLiteral(".json");
    if (QFile::copy(source, target)) {
        cwn::Log::warn(QStringLiteral("HomeworkService: corrupt day file backed up to %1")
                              .arg(target));
        return true;
    }
    cwn::Log::error(QStringLiteral("HomeworkService: failed to back up corrupt file %1")
                        .arg(source));
    return false;
}

bool HomeworkService::writeDayItems(const QDate &date, const QJsonArray &items)
{
    const QString dirPath = homeworkDir();
    if (!QDir().mkpath(dirPath)) {
        cwn::Log::error(QStringLiteral("HomeworkService: cannot create dir %1").arg(dirPath));
        return false;
    }

    // QSaveFile 原子写：临时文件 + commit 原子替换，写失败/断电不丢旧内容。
    // directWriteFallback 必须关掉：默认 true 时临时文件创建失败会绕过原子性
    // 直接截断写目标文件；Text 标志去掉（写方向转换无保证，且干扰字节数校验）
    QSaveFile file(dayFilePath(date));
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        cwn::Log::error(QStringLiteral("HomeworkService: cannot write %1 (%2)")
                            .arg(dayFilePath(date))
                            .arg(file.errorString()));
        return false;
    }
    QJsonObject root;
    root.insert(QStringLiteral("date"), date.toString(QLatin1String(kDatePattern)));
    root.insert(QStringLiteral("items"), items);
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size()) {
        cwn::Log::error(QStringLiteral("HomeworkService: short write on %1 (%2)")
                            .arg(dayFilePath(date))
                            .arg(file.errorString()));
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        cwn::Log::error(QStringLiteral("HomeworkService: commit failed on %1 (%2)")
                            .arg(dayFilePath(date))
                            .arg(file.errorString()));
        return false;
    }
    return true;
}

QVariantList HomeworkService::toVariantItems(const QJsonArray &items) const
{
    QVariantList out;
    out.reserve(items.size());
    for (const QJsonValue &value : items)
        out.append(value.toObject().toVariantMap());
    return out;
}
