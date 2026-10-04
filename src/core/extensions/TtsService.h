#pragma once

#include <QObject>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <QJsonValue>

#ifndef CWN_NO_TTS
#include <QTextToSpeech>
#endif

class ConfigStore;
class NotificationService;

// 语音播报扩展服务（classwidgets.ext.tts，本仓库自有模型，功能蓝本 cw2-tts）。
//
// 职责：监听 NotificationService::notified → 按 provider 后缀套模板
// （class/activity/break/free/preparation，变量来自 ScheduleRuntime 当前/下一节）
// → QTextToSpeech::say() 串行朗读（排他式 FIFO：当前句播完再取队首；有意偏离
// cw2-tts 的 last-wins 打断模型，避免打断时半句截断；不开线程、无临时文件）。
// 队列有界 + 单条限长 + TTL（见 cpp 顶部 kMaxQueue/kMaxTextLen/kQueueTtlMs），
// 防连发通知无节制堆积成几分钟的陈旧播报。
// engine "auto" 下合成失败自动换下一后端重试（对齐 cw2-tts 故障转移）；
// provider 级朗读范围开关存 extensions.tts.provider_enabled（缺省 true）。
//
// QML 经上下文名 "Tts" 与 AppCentral.tts 访问。注意设置页必须走
// AppCentral.tts，不能裸写 Tts.*——文件名隐式类型遮蔽上下文属性
// （同 RollCall.qml 教训）。
//
// 构建：Qt TextToSpeech 模块缺失时以 CWN_NO_TTS 编译为桩（healthy=false，
// 设置页只读提示，不阻塞主构建；见 CMakeLists）。
class TtsService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString engine READ engine NOTIFY engineChanged)
    Q_PROPERTY(QString activeEngine READ activeEngine NOTIFY engineChanged)
    Q_PROPERTY(QString voice READ voice NOTIFY voiceChanged)
    Q_PROPERTY(double volume READ volume NOTIFY volumeChanged)
    Q_PROPERTY(QVariantList voiceList READ voiceList NOTIFY voiceListChanged)
    Q_PROPERTY(QStringList availableEngines READ availableEngines NOTIFY enginesChanged)
    // 通知来源列表（绑定式，供设置页 Repeater 直接 model 绑定；命令式
    // notificationProviders() 保留给首帧/调试调用。变更经 providersChanged
    // 转发 NotificationService::notificationProvidersChanged）
    Q_PROPERTY(QVariantList providers READ notificationProviders NOTIFY providersChanged)
    Q_PROPERTY(bool healthy READ healthy NOTIFY healthChanged)
    Q_PROPERTY(QString healthMessage READ healthMessage NOTIFY healthChanged)
    Q_PROPERTY(bool speaking READ speaking NOTIFY speakingChanged)

public:
    explicit TtsService(ConfigStore *configs, NotificationService *notification,
                        QObject *runtime, QObject *parent = nullptr);
    ~TtsService() override;

    // 延迟注入 ScheduleRuntime（AppCentral 侧：服务本体建在首次运行教程门之前，
    // 教程完成后才拿得到 ScheduleRuntime，见 AppCentral::initialize）。
    // 只读 property 来源，不持有头依赖、不接管生命周期。
    void setScheduleRuntimeSource(QObject *runtime);

    QString engine() const;
    QString activeEngine() const { return m_activeEngine; }
    QString voice() const;
    double volume() const;
    // 语音列表元素 {id, name, locale}：locale（如 zh-CN / en-US）供设置页按
    // 语言筛选，不是死数据（设置页 Tts.qml 依此过滤出当前语言分组的语音）。
    QVariantList voiceList() const { return m_voiceList; }
    // 可选引擎名列表，"auto" 恒居首，其余为 QTextToSpeech::availableEngines()。
    // 返回类型保持 QStringList 不变（设置页 ComboBox 直接 model 绑定）。
    // 不可用标记降级：Qt 层只有全量名 API、无逐项可用性查询，故不标"不可用"
    // 灰态（靠切换后健康态反馈）；该偏离已在计划文档登记。
    QStringList availableEngines() const;
    bool healthy() const { return m_healthy; }
    QString healthMessage() const { return m_healthMessage; }
    bool speaking() const { return m_speaking; }

    // ── QML 契约 ──
    // 引擎切换（"auto"=自动选后端；未知后端名回退默认引擎）：写配置并立即
    // 落盘（用户显式操作），清空故障记忆，重建合成器并刷新语音列表。
    // 同时清空 extensions.tts.voice：语音 ID 跨引擎不通用（对齐 cw2-tts），
    // 否则设置页显示旧语音名却静默回落到引擎默认。
    Q_INVOKABLE void setEngine(const QString &engineName);
    // 语音切换（Qt 语音名，""=引擎默认）：写配置并落盘，热应用到合成器。
    Q_INVOKABLE void setVoice(const QString &voiceId);
    // 音量 0~1（越界钳位）：写配置（靠自动保存落盘，滑杆拖动不逐刻写盘），
    // 即时应用到合成器。
    Q_INVOKABLE void setVolume(double volume);
    // 五类模板键（固定顺序，对齐 cw2-tts tconfig.py）
    Q_INVOKABLE QStringList templateKeys() const;
    Q_INVOKABLE QString getTemplate(const QString &key) const;
    Q_INVOKABLE void setTemplate(const QString &key, const QString &templateText);
    Q_INVOKABLE QString defaultTemplate(const QString &key) const;
    Q_INVOKABLE void resetTemplate(const QString &key);
    // 显式朗读（设置页试听/模板试听）：不受扩展开关限制，用户点按即读。
    Q_INVOKABLE void testSpeak(const QString &text);
    Q_INVOKABLE void stopSpeaking();
    // 朗读范围：provider 缺省 true（缺键即朗读）；写整表回读（provider_id 含
    // 点，不能走点分路径直写，jsonSetAt 会按点切分键）。
    Q_INVOKABLE bool providerEnabled(const QString &providerId) const;
    Q_INVOKABLE void setProviderEnabled(const QString &providerId, bool enabled);
    // 通知来源列表（委托 NotificationService，形状 {id,name,icon,...}，
    // 含作业/点名等后来注册的 provider，动态列出）。
    Q_INVOKABLE QVariantList notificationProviders() const;
    // 语音列表为同步枚举（Qt 层快）；引擎切换后自动刷新，本入口供手动刷新键。
    Q_INVOKABLE void refreshVoices();

public slots:
    // 通知播报过滤链：扩展启用 → silent 跳过 → provider 过滤 → 模板构建 → 入队。
    // 只读订阅 notified，不碰 playNotificationSound 路径（现有铃声不受影响）。
    void onNotified(const QVariantMap &payload);

signals:
    void engineChanged();
    void voiceChanged();
    void volumeChanged();
    void voiceListChanged();
    void enginesChanged();
    void healthChanged();
    void speakingChanged();
    void templatesChanged();
    // 通知来源注册表变化（透传 NotificationService::notificationProvidersChanged，
    // 含作业/点名等后来注册的 provider；设置页据此重建朗读范围列表）
    void providersChanged();

private slots:
    void onConfigChanged();
#ifndef CWN_NO_TTS
    void onSpeechStateChanged(QTextToSpeech::State state);
    void onSpeechError(QTextToSpeech::ErrorReason reason, const QString &errorString);
#endif

private:
    // 文案构建（对应 cw2-tts announcer.py 全规则）：provider_id 后缀映射
    // .class/.activity/.break/.free/.preparation → 模板 format；模板残留
    // {…} 占位（变量名打错）回退 title。message；未知 provider 拼 title。message。
    QString buildAnnounceText(const QVariantMap &payload) const;
    // 运行时上下文（对应 main.py _build_runtime_context）：currentSubject
    // name/teacher/location、currentEntry.title 兜底、nextEntries[0] 经 subjects
    // 解析 next_*（解析不到回退 title）。
    QVariantMap runtimeContext() const;
    static QString applyTemplate(const QString &tmpl, const QString &title,
                                 const QString &message, const QVariantMap &ctx,
                                 bool *ok);
    bool isExtensionEnabled() const;
    bool isProviderAllowed(const QString &providerId) const;
    QVariantMap readTemplates() const;
    void writeMapEntry(const QString &mapKey, const QString &entryKey,
                       const QJsonValue &value);
    void enqueueSpeak(const QString &text);
    void startSpeak(const QString &text);
    // 队首取句播报（唯一的接续入口）：跳过 TTL 过期项，取到有效句即 startSpeak，
    // 取空则复位当前句与 speaking。统一经 scheduleQueuePump 延迟调用，
    // 避免在 stateChanged/errorOccurred 栈内重入 say()。
    void speakNextFromQueue();
    void scheduleQueuePump();
    void setSpeaking(bool speaking);
    void updateHealth(bool ok, const QString &message = {});
#ifndef CWN_NO_TTS
    void initSpeech();
    void applyVoiceToSpeech();
    void rebuildVoiceList();
#endif

    ConfigStore *m_configs = nullptr;
    NotificationService *m_notification = nullptr;
    QObject *m_runtime = nullptr; // ScheduleRuntime（只读 property，不持有头依赖）
#ifndef CWN_NO_TTS
    QTextToSpeech *m_speech = nullptr;
    QSet<QString> m_failedEngines; // auto 故障转移已证伪的后端
#endif
    // 待朗读句 + 入队时刻（enqueuedMs 供 TTL 判过期，见 cpp kQueueTtlMs）。
    struct QueuedSpeech {
        QString text;
        qint64 enqueuedMs = 0;
    };
    QString m_activeEngine; // 实际合成后端名（auto 下为真实选中的后端）
    QVariantList m_voiceList; // 元素 {id, name, locale}
    QList<QueuedSpeech> m_queue; // 待朗读队列（有界 + TTL，排他式 FIFO）
    QString m_currentText;    // 正在朗读句（auto 故障转移重试用）
    bool m_speaking = false;
    bool m_healthy = false;
    QString m_healthMessage;
    bool m_applying = false; // 自写配置回环抑制（set 经 dataChanged 同步回授）
    bool m_failoverPending = false; // auto 故障转移进行中（重入守卫）
    bool m_queuePumpPending = false; // 队列接续已排队（防多次 say 顶掉前一句）
};
