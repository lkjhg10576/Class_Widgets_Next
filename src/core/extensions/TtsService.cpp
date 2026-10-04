#include "TtsService.h"

#include "ConfigStore.h"
#include "Logger.h"
#include "../notification/NotificationService.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>

#include <optional>

namespace {
// 配置键（见 ConfigStore::defaultConfig extensions.tts）
const char kEngineKey[] = "extensions.tts.engine";
const char kVoiceKey[] = "extensions.tts.voice";
const char kVolumeKey[] = "extensions.tts.volume";
const char kTemplatesKey[] = "extensions.tts.templates";
const char kProviderEnabledKey[] = "extensions.tts.provider_enabled";
const char kEnabledListKey[] = "extensions.enabled";
const char kExtensionId[] = "classwidgets.ext.tts";
const char kAutoEngine[] = "auto";

// 队列护栏（连发通知时的自保；排他式 FIFO 的取句与判过期在 speakNextFromQueue）
constexpr int kMaxQueue = 8;             // 待朗读上限，溢出丢最旧（连带日志）
constexpr qsizetype kMaxTextLen = 300;   // 单条限长，超出截断（长标题/长消息保护）
constexpr qint64 kQueueTtlMs = 60000;    // 入队后 60s 仍未播到即作废（排他队列的必然残留）

// 默认模板（对齐 cw2-tts tconfig.py DEFAULT_TEMPLATES，源文中文）
const char *kTemplateKeys[] = { "class", "activity", "break", "free", "preparation", nullptr };

QString builtinDefaultTemplate(const QString &key)
{
    if (key == QLatin1String("class"))
        return QStringLiteral("上课了，{subject}");
    if (key == QLatin1String("activity"))
        return QStringLiteral("活动开始，{subject}");
    if (key == QLatin1String("break"))
        return QStringLiteral("下课了，下节课是{next_subject}");
    if (key == QLatin1String("free"))
        return QStringLiteral("放学了");
    if (key == QLatin1String("preparation"))
        return QStringLiteral("预备铃，下节课是{next_subject}");
    return {};
}

// provider_id 后缀 → 模板键（对应 announcer.py _PROVIDER_SUFFIX_MAP）
QString activityKeyForProvider(const QString &providerId)
{
    if (providerId.endsWith(QLatin1String(".class")))
        return QStringLiteral("class");
    if (providerId.endsWith(QLatin1String(".activity")))
        return QStringLiteral("activity");
    if (providerId.endsWith(QLatin1String(".break")))
        return QStringLiteral("break");
    if (providerId.endsWith(QLatin1String(".free")))
        return QStringLiteral("free");
    if (providerId.endsWith(QLatin1String(".preparation")))
        return QStringLiteral("preparation");
    return {};
}

// announcer.py 标点清理：result.strip("。，, .") + strip()
QString stripPunct(const QString &text)
{
    static const QSet<QChar> punct = { QLatin1Char('，'), QLatin1Char('。'),
                                       QLatin1Char(','), QLatin1Char('.'),
                                       QLatin1Char(' ') };
    int begin = 0;
    int end = text.size();
    while (begin < end && punct.contains(text.at(begin)))
        ++begin;
    while (end > begin && punct.contains(text.at(end - 1)))
        --end;
    return text.mid(begin, end - begin).trimmed();
}

std::optional<QJsonValue> readConfig(const ConfigStore *configs, const char *key)
{
    return configs ? configs->value(QLatin1String(key)) : std::nullopt;
}

#ifndef CWN_NO_TTS
// auto 故障转移的后端优先级（小值优先，对齐 cw2-tts 的显式后端优先级表）：
// Windows 上 winrt（SAPI5）质量与可用性优于 sapi（老接口），故优先；
// 其余后端同为 2，按 QTextToSpeech::availableEngines() 的返回顺序取先到者。
int failoverPriority(const QString &engineName)
{
    if (engineName == QLatin1String("winrt"))
        return 0;
    if (engineName == QLatin1String("sapi"))
        return 1;
    return 2;
}
#endif

// next_subject 为空时，模板里的"下节课是"会变成悬空短语（对齐 announcer.py
// 的同类清理）：连同前置分隔标点一并删除，随后由 stripPunct 收尾。
QString stripDanglingNextSubject(const QString &text)
{
    static const QRegularExpression dangling(
        QStringLiteral("[，,。\\.、；;：:]?\\s*下\\s*一\\s*节\\s*课\\s*是\\s*"));
    QString out = text;
    out.remove(dangling);
    return out;
}
} // namespace

TtsService::TtsService(ConfigStore *configs, NotificationService *notification,
                       QObject *runtime, QObject *parent)
    : QObject(parent)
    , m_configs(configs)
    , m_notification(notification)
    , m_runtime(runtime)
{
#ifdef CWN_NO_TTS
    updateHealth(false, tr("Qt TextToSpeech 模块不可用，语音播报已禁用"));
#else
    initSpeech();
#endif
    if (m_notification) {
        connect(m_notification, &NotificationService::notificationProvidersChanged, this,
                &TtsService::providersChanged);
    }
    if (m_configs)
        connect(m_configs, &ConfigStore::dataChanged, this, &TtsService::onConfigChanged);
}

TtsService::~TtsService()
{
#ifndef CWN_NO_TTS
    if (m_speech)
        m_speech->stop();
#endif
}

void TtsService::setScheduleRuntimeSource(QObject *runtime)
{
    // 简单赋值即可：读上下文只发生在 buildAnnounceText（通知到来时），
    // 此刻 ScheduleRuntime 早已就绪；换指针不需要刷新任何缓存状态
    m_runtime = runtime;
}

// ─────────────────────────── 属性读取 ───────────────────────────

QString TtsService::engine() const
{
    if (const auto v = readConfig(m_configs, kEngineKey))
        return v->toString(QString::fromLatin1(kAutoEngine));
    return QString::fromLatin1(kAutoEngine);
}

QString TtsService::voice() const
{
    if (const auto v = readConfig(m_configs, kVoiceKey))
        return v->toString();
    return {};
}

double TtsService::volume() const
{
    if (const auto v = readConfig(m_configs, kVolumeKey))
        return qBound(0.0, v->toDouble(1.0), 1.0);
    return 1.0;
}

QStringList TtsService::availableEngines() const
{
    QStringList engines{ QString::fromLatin1(kAutoEngine) };
#ifndef CWN_NO_TTS
    engines.append(QTextToSpeech::availableEngines());
#endif
    return engines;
}

QVariantList TtsService::notificationProviders() const
{
    return m_notification ? m_notification->notificationProviders() : QVariantList{};
}

// ─────────────────────────── 写回 ───────────────────────────

void TtsService::setEngine(const QString &engineName)
{
    const QString normalized =
        engineName.trimmed().isEmpty() ? QString::fromLatin1(kAutoEngine) : engineName.trimmed();
    // 值相等短路（设置页 refresh() 程序化赋值同样触发 onToggled，不短路则每次
    // dataChanged 都带来一次 save() 空转落盘）；但非健康态下同值重选视为手动
    // 重试（清空故障记忆、重建合成器），对齐"试一次"的用户意图
    if (normalized == engine() && m_healthy)
        return;
    m_applying = true;
    // 跨引擎语音 ID 不通用（对齐 cw2-tts）：换引擎即清语音，否则新引擎按旧名
    // 匹配不到、applyVoiceToSpeech 每次热应用都打一条 debug，且用户看到的是
    // "选了语音却不生效"。与引擎写入同一批（m_applying 已抑制 dataChanged 回授），
    // 结尾只落盘一次。
    bool voiceCleared = false;
    if (m_configs) {
        m_configs->set(QLatin1String(kEngineKey), normalized);
        if (!voice().isEmpty()) {
            m_configs->set(QLatin1String(kVoiceKey), QString());
            voiceCleared = true;
            cwn::Log::info(QStringLiteral("TtsService: engine '%1' changed, voice cleared")
                               .arg(normalized));
        }
        m_configs->save(); // 用户显式操作即时落盘（同 setEnabled 语义）
    }
#ifndef CWN_NO_TTS
    m_failedEngines.clear();
    initSpeech();
#endif
    m_applying = false;
    emit engineChanged();
    emit enginesChanged();
    if (voiceCleared)
        emit voiceChanged(); // 语音已被清空，重建后告知设置页回填为空
}

void TtsService::setVoice(const QString &voiceId)
{
    if (voiceId == voice())
        return; // 同 setEngine：防设置页程序化 onToggled 带来的 save() 空转
    m_applying = true;
    if (m_configs) {
        m_configs->set(QLatin1String(kVoiceKey), voiceId);
        m_configs->save();
    }
#ifndef CWN_NO_TTS
    applyVoiceToSpeech();
#endif
    m_applying = false;
    emit voiceChanged();
}

void TtsService::setVolume(double volume)
{
    const double clamped = qBound(0.0, volume, 1.0);
    if (qFuzzyCompare(clamped + 1.0, this->volume() + 1.0))
        return; // 同上：滑杆 refresh() 程序化赋值不产生写回
    m_applying = true;
    if (m_configs)
        m_configs->set(QLatin1String(kVolumeKey), clamped); // 滑杆拖动靠自动保存落盘
#ifndef CWN_NO_TTS
    if (m_speech)
        m_speech->setVolume(clamped);
#endif
    m_applying = false;
    emit volumeChanged();
}

QStringList TtsService::templateKeys() const
{
    QStringList keys;
    for (int i = 0; kTemplateKeys[i] != nullptr; ++i)
        keys.append(QLatin1String(kTemplateKeys[i]));
    return keys;
}

QVariantMap TtsService::readTemplates() const
{
    QVariantMap out;
    if (const auto v = readConfig(m_configs, kTemplatesKey))
        out = v->toObject().toVariantMap();
    return out;
}

QString TtsService::getTemplate(const QString &key) const
{
    const QString stored = readTemplates().value(key).toString();
    // 空字符串是用户合法选择（该类不播报），只有"缺键"才回退默认
    if (readTemplates().contains(key))
        return stored;
    return defaultTemplate(key);
}

void TtsService::setTemplate(const QString &key, const QString &templateText)
{
    if (defaultTemplate(key).isNull())
        return; // 未知键拒绝（defaultTemplate 空且非五类之一）
    if (getTemplate(key) == templateText)
        return; // 同 setEngine：防 refresh() 回填触发的 save() 空转
    m_applying = true;
    writeMapEntry(kTemplatesKey, key, QJsonValue(templateText));
    if (m_configs)
        m_configs->save();
    m_applying = false;
    emit templatesChanged();
}

QString TtsService::defaultTemplate(const QString &key) const
{
    return builtinDefaultTemplate(key);
}

void TtsService::resetTemplate(const QString &key)
{
    setTemplate(key, defaultTemplate(key));
}

bool TtsService::providerEnabled(const QString &providerId) const
{
    if (const auto v = readConfig(m_configs, kProviderEnabledKey)) {
        const QJsonObject map = v->toObject();
        if (map.contains(providerId))
            return map.value(providerId).toBool(true);
    }
    return true; // 缺省 true（缺键即朗读）
}

void TtsService::setProviderEnabled(const QString &providerId, bool enabled)
{
    if (providerId.isEmpty() || providerEnabled(providerId) == enabled)
        return; // 同 setEngine：防朗读范围行程序化 onToggled 的整表 save() 空转
    m_applying = true;
    writeMapEntry(kProviderEnabledKey, providerId, QJsonValue(enabled));
    if (m_configs)
        m_configs->save();
    m_applying = false;
}

void TtsService::refreshVoices()
{
#ifndef CWN_NO_TTS
    rebuildVoiceList();
#else
    if (!m_voiceList.isEmpty()) {
        m_voiceList.clear();
        emit voiceListChanged();
    }
#endif
}

void TtsService::writeMapEntry(const QString &mapKey, const QString &entryKey,
                               const QJsonValue &value)
{
    // 整表读-改-写：entryKey 可能含点（如 provider_id），不能走点分路径直写
    QJsonObject map;
    if (const auto v = readConfig(m_configs, mapKey.toLatin1().constData()))
        map = v->toObject();
    map.insert(entryKey, value);
    if (m_configs)
        m_configs->set(mapKey, map.toVariantMap());
}

// ─────────────────────────── 朗读控制 ───────────────────────────

void TtsService::testSpeak(const QString &text)
{
    if (!text.trimmed().isEmpty())
        enqueueSpeak(text.trimmed());
}

void TtsService::stopSpeaking()
{
    m_queue.clear();
    m_currentText.clear();
#ifndef CWN_NO_TTS
    if (m_speech)
        m_speech->stop();
#endif
    setSpeaking(false);
}

void TtsService::enqueueSpeak(const QString &text)
{
    QString normalized = text.trimmed();
    if (normalized.isEmpty())
        return;
#ifdef CWN_NO_TTS
    cwn::Log::debug(QStringLiteral("TtsService: TextToSpeech unavailable, skip '%1'")
                        .arg(normalized));
    return;
#else
    if (!m_speech)
        return;
    if (normalized.size() > kMaxTextLen) {
        normalized.truncate(kMaxTextLen);
        cwn::Log::debug(QStringLiteral("TtsService: text over %1 chars, truncated")
                            .arg(static_cast<int>(kMaxTextLen)));
    }
    if (m_speaking || !m_queue.isEmpty()) {
        // 有界：连发通知时丢最旧（排他队列播得慢，越靠前越陈旧）
        if (m_queue.size() >= kMaxQueue) {
            const QString dropped = m_queue.takeFirst().text;
            cwn::Log::warn(QStringLiteral("TtsService: queue full (%1), drop oldest '%2'")
                               .arg(kMaxQueue)
                               .arg(dropped.left(24)));
        }
        m_queue.append(QueuedSpeech{ normalized, QDateTime::currentMSecsSinceEpoch() });
        return; // 排他式 FIFO：当前句播完再取队首（不打断，避免半句截断）
    }
    startSpeak(normalized);
#endif
}

void TtsService::startSpeak(const QString &text)
{
#ifdef CWN_NO_TTS
    Q_UNUSED(text);
#else
    if (!m_speech)
        return;
    m_currentText = text;
    // 先置 speaking 再 say：部分后端 say() 极快、状态同步回到 Ready，
    // 顺序反了会被 onSpeechStateChanged 的同步赋值抹掉 speaking（latch 不上，
    // 之后整条队列不再接续）。
    setSpeaking(true);
    m_speech->say(text);
#endif
}

void TtsService::scheduleQueuePump()
{
    if (m_queuePumpPending)
        return; // 已排一次，避免 Ready 与 error 各排一次导致连续 say 顶掉前句
    m_queuePumpPending = true;
    // 0ms 延迟出栈：stateChanged/errorOccurred 栈内重入 say 会被后端顶掉或崩
    QTimer::singleShot(0, this, &TtsService::speakNextFromQueue);
}

void TtsService::speakNextFromQueue()
{
    m_queuePumpPending = false;
#ifndef CWN_NO_TTS
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    while (!m_queue.isEmpty()) {
        const QueuedSpeech item = m_queue.takeFirst();
        if (item.enqueuedMs > 0 && now - item.enqueuedMs > kQueueTtlMs) {
            cwn::Log::debug(QStringLiteral("TtsService: queue item expired after %1ms, drop '%2'")
                                .arg(kQueueTtlMs)
                                .arg(item.text.left(24)));
            continue;
        }
        startSpeak(item.text);
        return;
    }
#endif
    // 取空：复位当前句。仅在静默态复位——入队路径可能在本函数相邻时段内
    // 同步起播了下一句（onNotified 到达），此时清状态会误报 speaking=false。
    if (!m_speaking) {
        m_currentText.clear();
        setSpeaking(false);
    }
}

void TtsService::setSpeaking(bool speaking)
{
    if (m_speaking == speaking)
        return;
    m_speaking = speaking;
    emit speakingChanged();
}

void TtsService::updateHealth(bool ok, const QString &message)
{
    if (ok == m_healthy && message == m_healthMessage)
        return;
    m_healthy = ok;
    m_healthMessage = message;
    emit healthChanged();
}

// ─────────────────────────── 通知过滤链 ───────────────────────────

void TtsService::onNotified(const QVariantMap &payload)
{
    const QString providerId = payload.value(QStringLiteral("provider_id")).toString();
    // 每个跳过分支各留一条 debug（含 provider_id 与原因）："该播的没播"时
    // 只看健康态排查不到，日志是唯一的过滤链轨迹
    if (!isExtensionEnabled()) {
        cwn::Log::debug(QStringLiteral("TtsService: extension '%1' disabled, skip '%2'")
                            .arg(QString::fromLatin1(kExtensionId), providerId));
        return;
    }
    if (payload.value(QStringLiteral("silent"), false).toBool()) {
        cwn::Log::debug(QStringLiteral("TtsService: silent payload, skip '%1'").arg(providerId));
        return; // silent 标记跳过（与通知铃声语义一致）
    }
    if (!isProviderAllowed(providerId)) {
        cwn::Log::debug(QStringLiteral("TtsService: provider '%1' switched off, skip")
                            .arg(providerId));
        return;
    }
    const QString text = buildAnnounceText(payload);
    if (text.isEmpty()) {
        cwn::Log::debug(QStringLiteral("TtsService: empty announce text, skip '%1'")
                            .arg(providerId));
        return;
    }
    enqueueSpeak(text);
}

bool TtsService::isExtensionEnabled() const
{
    if (const auto v = readConfig(m_configs, kEnabledListKey)) {
        for (const QJsonValue &e : v->toArray()) {
            if (e.toString() == QLatin1String(kExtensionId))
                return true;
        }
    }
    return false;
}

bool TtsService::isProviderAllowed(const QString &providerId) const
{
    return providerEnabled(providerId);
}

QString TtsService::buildAnnounceText(const QVariantMap &payload) const
{
    const QString providerId = payload.value(QStringLiteral("provider_id")).toString();
    const QString title = payload.value(QStringLiteral("title")).toString().trimmed();
    const QString message = payload.value(QStringLiteral("message")).toString().trimmed();
    const QVariantMap ctx = runtimeContext();

    const QString activityKey = activityKeyForProvider(providerId);
    if (!activityKey.isEmpty()) {
        QString tmpl = readTemplates().value(activityKey).toString();
        if (!readTemplates().contains(activityKey))
            tmpl = defaultTemplate(activityKey);
        // 空模板是用户显式清空 = 该类不播报（不是异常，走返回空串而非回退）
        if (tmpl.isEmpty()) {
            cwn::Log::debug(QStringLiteral("TtsService: template '%1' cleared, skip '%2'")
                                .arg(activityKey, providerId));
            return {};
        }
        bool ok = false;
        QString result = applyTemplate(tmpl, title, message, ctx, &ok);
        if (!ok) {
            cwn::Log::warn(
                QStringLiteral("TtsService: template '%1' render failed for '%2', fallback to title/message")
                    .arg(activityKey, providerId));
            // 模板格式化异常回退（对应 announcer.py except 分支）
            result = message.isEmpty() ? title : title + QStringLiteral("。") + message;
        } else {
            // next_subject 解析不到时清掉悬空的"下节课是"，否则播"下课了，下节课是"
            if (ctx.value(QStringLiteral("next_subject")).toString().isEmpty())
                result = stripDanglingNextSubject(result);
            result = stripPunct(result);
        }
        return result;
    }

    // 非日程通知兜底：title。message（对应 announcer.py 通用拼接）
    if (!title.isEmpty() && !message.isEmpty())
        return title + QStringLiteral("。") + message;
    return title.isEmpty() ? message : title;
}

QString TtsService::applyTemplate(const QString &tmpl, const QString &title,
                                  const QString &message, const QVariantMap &ctx, bool *ok)
{
    // 单遍扫描替换：占位符与其替换值一并定稿。旧实现是 8 次串行 replace，
    // 会把"值里的 {…}"当成占位做二次替换（课程名含花括号即踩），且残留判据
    // 同样误报。这里只认捕获到的占位名，未知名原样保留并置 ok=false（整句回退）。
    static const QRegularExpression placeholder(QStringLiteral("\\{([^{}]+)\\}"));
    QVariantMap values;
    values.insert(QStringLiteral("title"), title);
    values.insert(QStringLiteral("message"), message);
    values.insert(QStringLiteral("subject"), ctx.value(QStringLiteral("subject")).toString());
    values.insert(QStringLiteral("teacher"), ctx.value(QStringLiteral("teacher")).toString());
    values.insert(QStringLiteral("location"), ctx.value(QStringLiteral("location")).toString());
    values.insert(QStringLiteral("next_subject"),
                  ctx.value(QStringLiteral("next_subject")).toString());
    values.insert(QStringLiteral("next_teacher"),
                  ctx.value(QStringLiteral("next_teacher")).toString());
    values.insert(QStringLiteral("next_location"),
                  ctx.value(QStringLiteral("next_location")).toString());

    QString result;
    result.reserve(tmpl.size());
    bool complete = true;
    int last = 0;
    auto it = placeholder.globalMatch(tmpl);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        result += tmpl.mid(last, match.capturedStart() - last);
        last = match.capturedEnd();
        const auto found = values.constFind(match.captured(1));
        if (found == values.constEnd()) {
            complete = false; // 未知变量名（Python 侧即 KeyError）→ 整句回退
            result += match.captured(0);
        } else {
            result += found.value().toString(); // 值里的花括号不再参与匹配
        }
    }
    result += tmpl.mid(last);
    *ok = complete;
    return result;
}

QVariantMap TtsService::runtimeContext() const
{
    QVariantMap ctx{ { QStringLiteral("subject"), QString() },
                     { QStringLiteral("teacher"), QString() },
                     { QStringLiteral("location"), QString() },
                     { QStringLiteral("next_subject"), QString() },
                     { QStringLiteral("next_teacher"), QString() },
                     { QStringLiteral("next_location"), QString() } };
    if (!m_runtime)
        return ctx;

    const QVariantMap currentSubject = m_runtime->property("currentSubject").toMap();
    ctx.insert(QStringLiteral("subject"), currentSubject.value(QStringLiteral("name")).toString());
    ctx.insert(QStringLiteral("teacher"),
               currentSubject.value(QStringLiteral("teacher")).toString());
    ctx.insert(QStringLiteral("location"),
               currentSubject.value(QStringLiteral("location")).toString());

    // 当前条目 title 兜底（活动等无 subject 时）
    if (ctx.value(QStringLiteral("subject")).toString().isEmpty()) {
        const QVariantMap currentEntry = m_runtime->property("currentEntry").toMap();
        ctx.insert(QStringLiteral("subject"),
                   currentEntry.value(QStringLiteral("title")).toString());
    }

    const QVariantList nextEntries = m_runtime->property("nextEntries").toList();
    if (!nextEntries.isEmpty()) {
        const QVariantMap nextEntry = nextEntries.first().toMap();
        const QString nextSid = nextEntry.value(QStringLiteral("subjectId")).toString();
        bool resolved = false;
        if (!nextSid.isEmpty()) {
            const QVariantList subjects = m_runtime->property("subjects").toList();
            for (const QVariant &s : subjects) {
                const QVariantMap subject = s.toMap();
                if (subject.value(QStringLiteral("id")).toString() == nextSid) {
                    ctx.insert(QStringLiteral("next_subject"),
                               subject.value(QStringLiteral("name")).toString());
                    ctx.insert(QStringLiteral("next_teacher"),
                               subject.value(QStringLiteral("teacher")).toString());
                    ctx.insert(QStringLiteral("next_location"),
                               subject.value(QStringLiteral("location")).toString());
                    resolved = true;
                    break;
                }
            }
        }
        if (!resolved)
            ctx.insert(QStringLiteral("next_subject"),
                       nextEntry.value(QStringLiteral("title")).toString());
    }
    return ctx;
}

void TtsService::onConfigChanged()
{
    if (m_applying || !m_configs)
        return;
    // 外部改写配置（插件 API/锁键）时热应用：引擎变化重建，其余即时生效
#ifndef CWN_NO_TTS
    if (m_speech) {
        if (engine() != m_activeEngine && engine() != QLatin1String(kAutoEngine)
            && m_speech->engine() != engine()) {
            // 显式引擎被改：重建（auto 的 activeEngine 本就不同于配置值，不触发）
            initSpeech();
            emit engineChanged();
        } else {
            applyVoiceToSpeech();
            m_speech->setVolume(volume());
            emit voiceChanged();
            emit volumeChanged();
        }
    }
#else
    emit voiceChanged();
    emit volumeChanged();
#endif
    emit templatesChanged();
}

#ifndef CWN_NO_TTS
// ─────────────────────────── 合成器（真实后端） ───────────────────────────

void TtsService::initSpeech()
{
    delete m_speech;
    m_speech = nullptr;
    m_queue.clear();
    m_currentText.clear();
    setSpeaking(false);

    const QString configured = engine();
    QString explicitEngine;
    if (configured != QLatin1String(kAutoEngine) && !configured.isEmpty()) {
        if (QTextToSpeech::availableEngines().contains(configured)) {
            explicitEngine = configured;
        } else {
            cwn::Log::warn(QStringLiteral("TtsService: unknown engine '%1', use default")
                               .arg(configured));
        }
    }
    m_speech = explicitEngine.isEmpty() ? new QTextToSpeech(this)
                                        : new QTextToSpeech(explicitEngine, this);
    connect(m_speech, &QTextToSpeech::stateChanged, this, &TtsService::onSpeechStateChanged);
    connect(m_speech, &QTextToSpeech::errorOccurred, this, &TtsService::onSpeechError);

    m_activeEngine = m_speech->engine();
    applyVoiceToSpeech();
    m_speech->setVolume(volume());
    rebuildVoiceList();

    if (m_speech->state() == QTextToSpeech::BackendError) {
        // 构造即失败（后端缺失/初始化异常）：不能落到"可用"分支，否则设置页
        // 显示健康却永远不出声，且 m_failedEngines 为空、故障转移也无从触发
        cwn::Log::error(QStringLiteral("TtsService: engine '%1' failed to initialize")
                            .arg(m_activeEngine));
        updateHealth(false, tr("TTS 引擎 %1 初始化失败").arg(m_activeEngine));
    } else if (QTextToSpeech::availableEngines().isEmpty()) {
        updateHealth(false, tr("无可用 TTS 语音引擎"));
    } else {
        updateHealth(true);
    }
}

void TtsService::applyVoiceToSpeech()
{
    if (!m_speech)
        return;
    const QString wanted = voice();
    if (wanted.isEmpty())
        return; // 空 = 引擎默认语音，不干预
    for (const QVoice &v : m_speech->availableVoices()) {
        if (v.name() == wanted) {
            m_speech->setVoice(v);
            return;
        }
    }
    cwn::Log::debug(
        QStringLiteral("TtsService: voice '%1' not in engine '%2', keep default")
            .arg(wanted, m_activeEngine));
}

void TtsService::rebuildVoiceList()
{
    QVariantList list;
    if (m_speech) {
        for (const QVoice &v : m_speech->availableVoices()) {
            list.append(QVariantMap{ { QStringLiteral("id"), v.name() },
                                     { QStringLiteral("name"), v.name() },
                                     { QStringLiteral("locale"), v.locale().name() } });
        }
    }
    if (list != m_voiceList) {
        m_voiceList = list;
        emit voiceListChanged();
    }
}

void TtsService::onSpeechStateChanged(QTextToSpeech::State state)
{
    const bool nowSpeaking = (state == QTextToSpeech::Speaking);
    if (nowSpeaking != m_speaking) {
        m_speaking = nowSpeaking;
        emit speakingChanged();
    }
    // 播完一句（回到 Ready）即接下一句；接续一律延到事件循环下一拍
    // （speakNextFromQueue），不在 stateChanged 栈内同步 say
    if (state == QTextToSpeech::Ready)
        scheduleQueuePump();
}

void TtsService::onSpeechError(QTextToSpeech::ErrorReason reason, const QString &errorString)
{
    if (reason == QTextToSpeech::ErrorReason::NoError || !m_speech)
        return;
    if (m_failoverPending) {
        // 重入：故障转移过程中的连带 error（被摘除的旧实例收尾、或新实例
        // 初始化即报错）不再触发第二轮转移，否则会连换数个后端
        cwn::Log::debug(QStringLiteral("TtsService: error during failover, ignored: %1")
                            .arg(errorString));
        return;
    }
    cwn::Log::warn(QStringLiteral("TtsService: engine '%1' failed: %2")
                       .arg(m_activeEngine, errorString));
    if (engine() != QLatin1String(kAutoEngine)) {
        // 非 auto：只放弃当前句，队列继续（对齐 cw2-tts _on_synthesis_failed
        // 放弃本次合成；整链丢弃会让一句失败连坐后续所有播报）
        m_currentText.clear();
        setSpeaking(false);
        scheduleQueuePump();
        return;
    }
    m_failoverPending = true;

    if (!m_activeEngine.isEmpty())
        m_failedEngines.insert(m_activeEngine);
    QString next;
    int bestPriority = 3;
    for (const QString &candidate : QTextToSpeech::availableEngines()) {
        if (m_failedEngines.contains(candidate))
            continue;
        const int priority = failoverPriority(candidate);
        if (priority < bestPriority) { // 同优先级保持 Qt 返回顺序（先到先得）
            bestPriority = priority;
            next = candidate;
        }
    }
    if (next.isEmpty()) {
        cwn::Log::warn(QStringLiteral("TtsService: all TTS engines failed, drop speech"));
        m_queue.clear();
        m_currentText.clear();
        setSpeaking(false);
        updateHealth(false, tr("所有 TTS 引擎均不可用"));
        m_failoverPending = false;
        return;
    }

    cwn::Log::warn(
        QStringLiteral("TtsService: auto failover %1 -> %2").arg(m_activeEngine, next));
    const QString retry = m_currentText;
    // 旧实例只 stop + deleteLater（栈内同步 delete 会踩正在派发的信号栈）；
    // 先 disconnect，避免它在被删前再抛一轮 error 打乱转移时序
    m_speech->stop();
    m_speech->disconnect(this);
    m_speech->deleteLater();
    m_speech = nullptr;

    m_speech = new QTextToSpeech(next, this);
    connect(m_speech, &QTextToSpeech::stateChanged, this, &TtsService::onSpeechStateChanged);
    connect(m_speech, &QTextToSpeech::errorOccurred, this, &TtsService::onSpeechError);
    m_activeEngine = m_speech->engine();
    applyVoiceToSpeech();
    m_speech->setVolume(volume());
    rebuildVoiceList();
    updateHealth(true);
    emit engineChanged();
    m_failoverPending = false;

    if (!retry.isEmpty()) {
        // 延迟重试句：new QTextToSpeech 构造后重试句不宜在本栈内 say
        const QString text = retry;
        QTimer::singleShot(0, this, [this, text] {
            if (!m_speech)
                return;
            startSpeak(text);
        });
    } else {
        // 无重试句（队列取句时才失败）：交给队列继续
        setSpeaking(false);
        scheduleQueuePump();
    }
}
#endif
