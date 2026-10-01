#include "RollCallService.h"

#include "ConfigStore.h"
#include "Logger.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QRandomGenerator>
#include <QStringList>
#include <QVariantMap>

namespace {
// 名单键：元素 {name, weight(-100~100)}，形状与钳位已由 ConfigStore::sanitize
// 的 normalizeRollCallNames 在 load 时规范，本类读写仍做防御（手改坏数据不崩）
const char kNamesKey[] = "extensions.roll_call.names";
const char kAvoidRepeatKey[] = "extensions.roll_call.avoid_repeat";

// 有效权重映射（extensions-feature-plan §10.5）：1 + weight/100，
// weight 已钳位在 [-100,100] → 有效权重 ∈ [0,2]；再钳一次防漂移
double effectiveWeight(int weight)
{
    return qBound(0.0, 1.0 + weight / 100.0, 2.0);
}
} // namespace

RollCallService::RollCallService(ConfigStore *configs, QObject *parent)
    : QObject(parent)
    , m_configs(configs)
{
}

// ─────────────────────────── 抽取 ───────────────────────────

QVariantList RollCallService::draw(int count)
{
    QVariantList result;
    if (count <= 0) {
        m_lastDraw = result;
        m_lastRequested = 0;
        emit drawCompleted(result);
        return result;
    }
    m_lastRequested = count;

    // 候选池：session 模式排除本会话已点过的人；single 模式每次从全体重抽
    const bool session = avoidRepeatMode() == QLatin1String("session");
    QVariantList pool;
    for (const QVariant &entry : readNames()) {
        const QString name = entry.toMap().value(QStringLiteral("name")).toString();
        if (session && m_sessionPicked.contains(name))
            continue;
        pool.append(entry);
    }

    // 不放回加权抽取：每轮从剩余候选按有效权重抽 1 人、抽中即移出。
    // count 超过可抽人数时循环自然在 pool 耗尽处停止 → 返回全部（结果长度
    // 即实际抽出人数，QML 侧据此提示"人数不足"）
    while (result.size() < count && !pool.isEmpty()) {
        double total = 0.0;
        for (const QVariant &entry : pool)
            total += effectiveWeight(entry.toMap().value(QStringLiteral("weight")).toInt());

        int pick = -1;
        if (total > 0.0) {
            // 轮盘赌：在 [0,total) 取点，沿累计权重下界命中
            double r = QRandomGenerator::global()->bounded(total);
            for (int i = 0; i < pool.size(); ++i) {
                r -= effectiveWeight(
                    pool.at(i).toMap().value(QStringLiteral("weight")).toInt());
                if (r < 0.0) {
                    pick = i;
                    break;
                }
            }
            // 浮点累加误差兜底：未命中则取最后一人，保证 pick 有效、不死循环
            if (pick < 0)
                pick = pool.size() - 1;
        } else {
            // 全员有效权重为 0（全部 -100%）的退化场景：加权失去依据，兜底
            // 选"等权抽取"而非返回空 —— 用户仍点得出人，比"点了没反应"
            // 更符合课堂直觉；空名单（pool 本就为空）由上面的循环条件拦住，
            // 不会走到这里
            pick = QRandomGenerator::global()->bounded(pool.size());
        }

        const QVariantMap picked = pool.at(pick).toMap();
        QVariantMap out;
        out.insert(QStringLiteral("name"), picked.value(QStringLiteral("name")));
        out.insert(QStringLiteral("weight"), picked.value(QStringLiteral("weight")));
        result.append(out);
        pool.remove(pick);
    }

    if (session) {
        for (const QVariant &entry : result)
            m_sessionPicked.insert(entry.toMap().value(QStringLiteral("name")).toString());
    }

    m_lastDraw = result;
    emit drawCompleted(result);
    return result;
}

// ─────────────────────────── 名单导入 ───────────────────────────

QVariantList RollCallService::importNamesFromUrl(const QUrl &url)
{
    QVariantList out;
    const QString path = url.toLocalFile();
    if (path.isEmpty()) {
        cwn::Log::warn(QStringLiteral("RollCallService: import from invalid url: %1")
                           .arg(url.toString()));
        return out;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        cwn::Log::warn(QStringLiteral("RollCallService: cannot open import file: %1").arg(path));
        return out;
    }
    const QByteArray raw = file.readAll();

    // UTF-8 BOM（EF BB BF）：剥文件头字节序标记，否则首行名字带上不可见字符
    QByteArray body = raw;
    if (body.startsWith("\xEF\xBB\xBF"))
        body.remove(0, 3);

    // 每行一个名字；trim 顺带吃掉 \r 与空白；文件内去重（保留先出现者）。
    // 与现有名单的合并去重由设置页完成（本方法保持纯解析、无副作用）
    QSet<QString> seen;
    for (const QString &rawLine : QString::fromUtf8(body).split(QLatin1Char('\n'))) {
        // U+FEFF 不属于 QChar::isSpace，trimmed 不吃它：逐行显式剥除
        QString line = rawLine;
        line.remove(QChar(0xFEFF));
        const QString name = line.trimmed();
        if (name.isEmpty() || seen.contains(name))
            continue; // 空行跳过
        seen.insert(name);
        QVariantMap entry;
        entry.insert(QStringLiteral("name"), name);
        entry.insert(QStringLiteral("weight"), 0); // 新名字默认等权
        out.append(entry);
    }
    return out;
}

// ─────────────────────────── 名单编辑 ───────────────────────────

int RollCallService::mergeNames(const QVariantList &entries)
{
    if (!m_configs || entries.isEmpty())
        return 0;

    QVariantList names = readNames();
    // 去重集合一次建好（含批内新名），避免每个名字对全名单做线性扫描
    QSet<QString> existing;
    existing.reserve(names.size() + entries.size());
    for (const QVariant &entry : names)
        existing.insert(entry.toMap().value(QStringLiteral("name")).toString());

    int added = 0;
    for (const QVariant &entry : entries) {
        const QVariantMap map = entry.toMap();
        // 兼容两种形状：importNamesFromUrl 的 {name, weight} 映射与裸字符串
        const QString name = (map.isEmpty()
                                  ? entry.toString()
                                  : map.value(QStringLiteral("name")).toString())
                                 .trimmed();
        if (name.isEmpty() || existing.contains(name))
            continue; // 空名与重名（含批内）跳过
        existing.insert(name);

        QVariantMap item;
        item.insert(QStringLiteral("name"), name);
        item.insert(QStringLiteral("weight"),
                    qBound(-100, map.value(QStringLiteral("weight")).toInt(), 100));
        names.append(item);
        ++added;
    }

    // 单次写回 + 落盘 + namesChanged（无新增时不产生任何写入与信号）
    if (added > 0)
        writeNames(names);
    return added;
}

bool RollCallService::addName(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return false;

    QVariantList names = readNames();
    for (const QVariant &entry : names) {
        if (entry.toMap().value(QStringLiteral("name")).toString() == trimmed)
            return false; // 重名拒绝（名字唯一是 session 排除集合按名字去重的前提）
    }
    QVariantMap added;
    added.insert(QStringLiteral("name"), trimmed);
    added.insert(QStringLiteral("weight"), 0);
    names.append(added);
    writeNames(names);
    return true;
}

bool RollCallService::updateName(int index, const QString &name, int weight)
{
    QVariantList names = readNames();
    if (index < 0 || index >= names.size())
        return false;
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return false;
    for (int i = 0; i < names.size(); ++i) {
        if (i != index
            && names.at(i).toMap().value(QStringLiteral("name")).toString() == trimmed)
            return false; // 与其他行重名
    }

    QVariantMap updated;
    updated.insert(QStringLiteral("name"), trimmed);
    updated.insert(QStringLiteral("weight"), qBound(-100, weight, 100));
    names[index] = updated;
    writeNames(names);
    return true;
}

bool RollCallService::removeName(int index)
{
    QVariantList names = readNames();
    if (index < 0 || index >= names.size())
        return false;
    names.removeAt(index);
    writeNames(names);
    return true;
}

void RollCallService::clearNames()
{
    writeNames(QVariantList{});
}

void RollCallService::clearSession()
{
    // 会话排除名单是运行期状态（不落盘）：结果窗口关闭时由 QML 调用清空，
    // 下一次点名视为新会话
    m_sessionPicked.clear();
}

// ─────────────────────────── 内部 ───────────────────────────

QVariantList RollCallService::readNames() const
{
    QVariantList names;
    if (!m_configs)
        return names;
    const auto value = m_configs->value(QLatin1String(kNamesKey));
    if (!value.has_value() || !value->isArray())
        return names;
    for (const QJsonValue &e : value->toArray()) {
        if (!e.isObject())
            continue;
        const QJsonObject obj = e.toObject();
        const QString name = obj.value(QLatin1String("name")).toString();
        if (name.isEmpty())
            continue; // 无名字条目无法渲染/抽取，丢弃（防御 sanitize 之外的坏数据）
        const int weight = qBound(-100,
                                  obj.value(QLatin1String("weight")).toInt(0), 100);
        QVariantMap entry;
        entry.insert(QStringLiteral("name"), name);
        entry.insert(QStringLiteral("weight"), weight);
        names.append(entry);
    }
    return names;
}

void RollCallService::writeNames(const QVariantList &names)
{
    if (!m_configs)
        return;
    // 经 ConfigStore::set 写回（点分路径 + 锁定检查 + 相等短路 + 脏标记）
    m_configs->set(QLatin1String(kNamesKey), names);
    // 名单是点名功能的核心数据，用户显式编辑后立即落盘，不等 1 分钟自动保存
    m_configs->save();
    emit namesChanged();
}

QString RollCallService::avoidRepeatMode() const
{
    if (m_configs) {
        if (const auto value = m_configs->value(QLatin1String(kAvoidRepeatKey));
            value.has_value() && value->isString()
            && value->toString() == QLatin1String("session"))
            return QStringLiteral("session");
    }
    return QStringLiteral("single"); // 缺键/坏值回退默认策略
}
