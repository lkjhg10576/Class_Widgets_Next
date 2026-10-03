#include "RollCallService.h"

#include "ConfigStore.h"
#include "Logger.h"
#include "../notification/NotificationModel.h"
#include "../notification/NotificationProvider.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QStringConverter>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>
#include <QXmlStreamReader>
// docx = OPC zip 包：解析/解压交给 Qt 内置 QZipReader（私有 API，Qt6::CorePrivate）。
// 质检修正：原手写 EOCD/中央目录解析 + `__has_include(<zlib.h>)` 门控的 raw inflate
// 在 MSVC（Qt 不导出公共 zlib 头）下整条 docx 路径静默失效，且负偏移越界、损坏流
// 可致 GUI 线程死循环——QZipReader 由 Qt 统一处理，三个问题一并消除
#include <QtCore/private/qzipreader_p.h>

#include <utility>

namespace {
// 名单键：元素 {name, weight(-100~100)}，形状与钳位已由 ConfigStore::sanitize
// 的 normalizeRollCallNames 在 load 时规范，本类读写仍做防御（手改坏数据不崩）
const char kNamesKey[] = "extensions.roll_call.names";
const char kAvoidRepeatKey[] = "extensions.roll_call.avoid_repeat";
const char kNotifyDurationKey[] = "extensions.roll_call.notify_duration";
const char kAnnounceProviderId[] = "com.classwidgets.rollcall";

// 有效权重映射（extensions-feature-plan §10.5）：1 + weight/100，
// weight 已钳位在 [-100,100] → 有效权重 ∈ [0,2]；再钳一次防漂移
double effectiveWeight(int weight)
{
    return qBound(0.0, 1.0 + weight / 100.0, 2.0);
}

// ── 名单文本解析（four-plugins 一期）──
// 去序号：`^\s*(\d+[.．、]|\(\d+\)|（\d+）|第\d+名)`（中英文括号/顿号全覆盖）
QRegularExpression serialPrefixRegex()
{
    static const QRegularExpression re(
        QStringLiteral("^\\s*(?:\\d+[.．、]|\\(\\d+\\)|（\\d+）|第\\d+\\s*名)\\s*"));
    return re;
}
// 行内分割：`,，;；、\t` 或 2+ 空格（单空格保留，允许“欧阳 修”类名字内单空格）
QStringList splitInlineNames(const QString &line)
{
    static const QRegularExpression sep(QStringLiteral("[,，;；、\\t]+| {2,}"));
    QStringList out;
    for (const QString &part : line.split(sep))
        out.append(part);
    return out;
}

QString cleanOneName(QString name)
{
    QString n = name.trimmed();
    n.remove(QChar(0xFEFF)); // 行内残留 BOM
    // 行内 `#` 注释：空格 + # 后全部截断（名字本身不含 #）
    const int hashAt = n.indexOf(QLatin1Char('#'));
    if (hashAt > 0 && n.at(hashAt - 1).isSpace())
        n = n.left(hashAt).trimmed();
    n.remove(serialPrefixRegex());
    return n.trimmed();
}

// 多编码解码：utf-8-sig（剥 BOM）→ utf-16（BOM/零字节启发）→ gbk → utf-8 宽松
QString decodeNameBytes(const QByteArray &raw)
{
    QByteArray body = raw;
    if (body.startsWith("\xEF\xBB\xBF"))
        body.remove(0, 3); // utf-8-sig
    // UTF-16 解码辅助：bigEndian 时先把字节对高低位交换成主机（小端）序。
    // 质检修正：原实现对 FE FF（BE）BOM 也直接按主机序解释，解出乱码
    auto decodeUtf16 = [](const QByteArray &bytes, bool bigEndian) {
        QByteArray b = bytes;
        if (bigEndian) {
            for (int i = 0; i + 1 < b.size(); i += 2)
                std::swap(b[i], b[i + 1]);
        }
        return QString::fromUtf16(reinterpret_cast<const char16_t *>(b.constData()),
                                  b.size() / 2);
    };
    auto hasCjkChar = [](const QString &s) {
        for (QChar c : s) {
            const ushort u = c.unicode();
            if ((u >= 0x4E00 && u <= 0x9FFF) || (u >= 0x3400 && u <= 0x4DBF))
                return true;
        }
        return false;
    };
    if (body.startsWith("\xFF\xFE") || body.startsWith("\xFE\xFF")) {
        const bool bigEndian = body.startsWith("\xFE\xFF");
        return decodeUtf16(body.mid(2), bigEndian);
    }
    // UTF-16 无 BOM 启发：大量 \0 交错
    int zeros = 0;
    for (int i = 0; i < qMin(64, body.size()); ++i) {
        if (body.at(i) == '\0')
            ++zeros;
    }
    if (zeros > 8) {
        // 无 BOM：LE/BE 都试，含 CJK 则采信（质检修正：原实现只试 LE，
        // BE 文件落回 UTF-8 解出夹 NUL 的乱码）
        const QString le = decodeUtf16(body, false);
        if (hasCjkChar(le))
            return le;
        const QString be = decodeUtf16(body, true);
        if (hasCjkChar(be))
            return be;
    }
    QString utf8 = QString::fromUtf8(body);
    // 非 UTF-8 字节（gbk 中文）会导致 U+FFFD：回退 GBK/GB18030 解码
    // （Qt6 原生 QStringDecoder，不依赖 Qt5Compat 的 QTextCodec；CMake 无需加模块。
    // 无 ICU/iconv 的构建上 decoder 可能无效，此时回退宽松 UTF-8，不崩。）
    if (!utf8.contains(QChar::ReplacementCharacter)) {
        // 无 BOM 的 UTF-16 纯 ASCII 名单（质检修正）：UTF-8 解出夹 NUL 的串，
        // 按 UTF-16LE 兜底重解
        if (zeros > 8 && utf8.contains(QChar(u'\0')))
            return decodeUtf16(body, false);
        return utf8;
    }
    if (auto gbk = QStringDecoder("GBK"); gbk.isValid()) {
        QString decoded = gbk.decode(body);
        if (!decoded.contains(QChar::ReplacementCharacter))
            return decoded;
    }
    if (auto conv = QStringDecoder("GB18030"); conv.isValid())
        return conv.decode(body);
    return utf8;
}

// word/document.xml → 行文本：<w:p> 分段、<w:br/>/<w:cr/> 换行、<w:tab/> 制表
// （质检修正：原实现漏 <w:tab/>，同段两 run 被 "张三⇥李四" 式制表隔开时拼成
// 一个名字，与文本路径的 \t 行内分割行为不一致），取全部 <w:t> 拼接。
QStringList docxXmlToLines(const QByteArray &xml)
{
    QStringList lines;
    QXmlStreamReader reader(xml);
    QString current;
    bool hasPara = false;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            const QString name = reader.name().toString();
            if (name == QLatin1String("p")) {
                if (hasPara) {
                    lines.append(current);
                    current.clear();
                }
                hasPara = true;
            } else if (name == QLatin1String("br") || name == QLatin1String("cr")) {
                current.append(QLatin1Char('\n'));
            } else if (name == QLatin1String("tab")) {
                current.append(QLatin1Char('\t'));
            } else if (name == QLatin1String("t")) {
                current += reader.readElementText();
            }
        } else if (reader.isEndElement() && reader.name().toString() == QLatin1String("p")) {
            lines.append(current);
            current.clear();
            hasPara = false;
        }
    }
    if (!current.isEmpty() || hasPara)
        lines.append(current);
    return lines;
}

// docx → 行文本。zip 解析/解压全部经 QZipReader（见文件头 include 处注释）：
// 包损坏 / 无 word/document.xml 时返回空列表，由调用方记 warn。
QStringList docxExtractLines(const QString &path)
{
    QZipReader zip(path);
    if (!zip.exists())
        return {};
    const QByteArray xml = zip.fileData(QStringLiteral("word/document.xml"));
    if (xml.isEmpty())
        return {};
    return docxXmlToLines(xml);
}

// 全文 → 名字列表：注释/序号/行内分割/去重（保留先出现者）
QStringList parseNameText(const QString &text)
{
    QStringList names;
    QSet<QString> seen;
    const QString normalized = QString(text).replace(QLatin1Char('\r'), QLatin1Char('\n'));
    for (const QString &rawLine : normalized.split(QLatin1Char('\n'))) {
        QString line = rawLine;
        line.remove(QChar(0xFEFF));
        line = line.trimmed();
        if (line.isEmpty())
            continue;
        if (line.startsWith(QLatin1Char('#')) || line.startsWith(QStringLiteral("＃")))
            continue; // `#` 注释行
        for (const QString &piece : splitInlineNames(line)) {
            const QString name = cleanOneName(piece);
            if (name.isEmpty() || seen.contains(name))
                continue;
            seen.insert(name);
            names.append(name);
        }
    }
    return names;
}
} // namespace

RollCallService::RollCallService(ConfigStore *configs, QObject *parent)
    : QObject(parent)
    , m_configs(configs)
{
    // 点名播报通知来源（灵动通知设置页可见/可关；level 用 Announcement）
    m_announceProvider = new NotificationProvider(
        QString::fromLatin1(kAnnounceProviderId), tr("Roll Call"),
        QStringLiteral("ic_fluent_people_team_20_regular"), false, nullptr, this);
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
    const bool wantDocx = path.endsWith(QLatin1String(".docx"), Qt::CaseInsensitive)
        || (raw.size() > 2 && raw.at(0) == 'P' && raw.at(1) == 'K');

    QStringList names;
    if (wantDocx) {
        const QStringList lines = docxExtractLines(path);
        if (!lines.isEmpty()) {
            names = parseNameText(lines.join(QLatin1Char('\n')));
        } else {
            cwn::Log::warn(QStringLiteral("RollCallService: docx has no readable "
                                          "word/document.xml: %1")
                               .arg(path));
        }
    } else {
        names = parseNameText(decodeNameBytes(raw));
    }

    // 与现有名单的合并去重由设置页经 mergeNames 完成（本方法保持纯解析、无副作用）
    QSet<QString> seen;
    for (const QString &name : names) {
        if (name.isEmpty() || seen.contains(name))
            continue;
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
    // 上游遗留批量判定：全批权重都在 1..100 且无负值 → 视为上游旧值，按
    // migrateUpstreamWeight 逐个换算；混合批原样通过。
    bool upstreamBatch = true;
    for (const QVariant &entry : entries) {
        const QVariantMap m = entry.toMap();
        if (m.isEmpty())
            continue;
        const int w = m.value(QStringLiteral("weight")).toInt();
        if (w < 1 || w > 100) {
            upstreamBatch = false;
            break;
        }
    }
    for (const QVariant &entry : entries) {
        const QVariantMap map = entry.toMap();
        // 兼容两种形状：importNamesFromUrl 的 {name, weight} 映射与裸字符串
        const QString name = cleanOneName(map.isEmpty()
                                              ? entry.toString()
                                              : map.value(QStringLiteral("name")).toString());
        if (name.isEmpty() || existing.contains(name))
            continue; // 空名与重名（含批内）跳过
        existing.insert(name);

        int weight = map.value(QStringLiteral("weight")).toInt();
        if (upstreamBatch && !map.isEmpty())
            weight = migrateUpstreamWeight(weight);
        QVariantMap item;
        item.insert(QStringLiteral("name"), name);
        item.insert(QStringLiteral("weight"), qBound(-100, weight, 100));
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

// ─────────────────────────── four-plugins 一期新增 ───────────────────────────

int RollCallService::migrateUpstreamWeight(int wOld)
{
    // 上游 1-100（默认 100）→ Next -100~+100：w_new=(w_old-100)*2/100，钳位输出
    const int clamped = qBound(1, wOld, 100);
    const double mapped = (clamped - 100) * 2.0 / 100.0;
    return qBound(-100, qRound(mapped), 100);
}

void RollCallService::resetWeights()
{
    QVariantList names = readNames();
    if (names.isEmpty())
        return;
    for (QVariant &entry : names) {
        QVariantMap map = entry.toMap();
        map.insert(QStringLiteral("weight"), 0);
        entry = map;
    }
    writeNames(names);
}

QVariantList RollCallService::testDraw(int count)
{
    // 试抽：同 draw() 的不放回加权轮盘赌，但只读快照、不碰会话/历史/信号
    QVariantList result;
    if (count <= 0)
        return result;
    QVariantList pool = readNames();
    const bool session = avoidRepeatMode() == QLatin1String("session");
    if (session) {
        for (int i = pool.size() - 1; i >= 0; --i) {
            if (m_sessionPicked.contains(pool.at(i).toMap().value(QStringLiteral("name")).toString()))
                pool.removeAt(i);
        }
    }
    while (result.size() < count && !pool.isEmpty()) {
        double total = 0.0;
        for (const QVariant &entry : pool)
            total += effectiveWeight(entry.toMap().value(QStringLiteral("weight")).toInt());
        int pick = -1;
        if (total > 0.0) {
            double r = QRandomGenerator::global()->bounded(total);
            for (int i = 0; i < pool.size(); ++i) {
                r -= effectiveWeight(pool.at(i).toMap().value(QStringLiteral("weight")).toInt());
                if (r < 0.0) {
                    pick = i;
                    break;
                }
            }
            if (pick < 0)
                pick = pool.size() - 1;
        } else {
            pick = QRandomGenerator::global()->bounded(pool.size());
        }
        result.append(pool.at(pick));
        pool.removeAt(pick);
    }
    return result;
}

void RollCallService::announce(const QStringList &names)
{
    if (!m_announceProvider || names.isEmpty())
        return;
    int durationSecs = 5;
    if (m_configs) {
        if (const auto v = m_configs->value(QLatin1String(kNotifyDurationKey)))
            durationSecs = qBound(2, v->toInt(5), 15);
    }
    // 播报前临时展开 hide 层（four-plugins §4.2"播报前临时恢复 hide 层/按钮，
    // 播后还原"）。展开与还原全权由本方法负责：还原计时器挂本服务（应用生命
    // 周期对象），悬浮窗（click_hide/播报期手动关窗路径）销毁不影响还原。
    static const char kHideStateKey[] = "interactions.hide.state";
    if (m_configs && !m_configs->isKeyLocked(QLatin1String(kHideStateKey))) {
        if (const auto hidden = m_configs->value(QLatin1String(kHideStateKey));
            hidden.has_value() && hidden->toBool()) {
            m_hideRestorePending = true;
            m_configs->set(QLatin1String(kHideStateKey), false);
        }
    }
    const QString title = tr("Roll call result");
    const QString message = names.join(QStringLiteral("、"));
    // NotificationProvider::push(level, title, message, durationMs, closable)
    m_announceProvider->push(cwn::notification::LevelAnnouncement, title, message,
                             durationSecs * 1000, true);
    if (!m_hideRestorePending)
        return;
    if (!m_hideRestoreTimer) {
        m_hideRestoreTimer = new QTimer(this);
        m_hideRestoreTimer->setSingleShot(true);
        connect(m_hideRestoreTimer, &QTimer::timeout, this,
                &RollCallService::restoreHiddenState);
        // 退出兜底：还原必须发生在 main() 的 configs->save() 落盘之前
        // （aboutToQuit 在 exec() 返回前发出），否则展开态会被持久化到重启后
        connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this] {
            if (m_hideRestoreTimer && m_hideRestoreTimer->isActive())
                restoreHiddenState();
        });
    }
    m_hideRestoreTimer->start(durationSecs * 1000); // 重复播报 = 重置还原窗口
}

void RollCallService::restoreHiddenState()
{
    if (m_hideRestoreTimer)
        m_hideRestoreTimer->stop();
    if (!m_configs || !m_hideRestorePending)
        return;
    m_hideRestorePending = false;
    static const char kHideStateKey[] = "interactions.hide.state";
    if (m_configs->isKeyLocked(QLatin1String(kHideStateKey)))
        return;
    // 仅当 hide 层仍处于播报临时展开态（false）才还原：播报期间用户手动
    // 显隐过则尊重新状态
    if (const auto v = m_configs->value(QLatin1String(kHideStateKey));
        v.has_value() && !v->toBool())
        m_configs->set(QLatin1String(kHideStateKey), true);
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
