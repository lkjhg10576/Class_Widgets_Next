#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class ConfigStore;

// M1 阶段的占位对象：M1 加载的 QML 会引用这些名字（见 CW2-M1骨架任务拆分.md §0.5.1
// 与 M1 探索报告），成员必须存在以免绑定报错；实现主体在 M2/M4 落地。
// 每个类都注明了对应的上游模块与计划中的落地里程碑。

// 对应 core/utils/translator.py AppTranslator —— M4 完整落地（加载 assets/locales/*.qm）
class TranslatorStub : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
public:
    explicit TranslatorStub(const ConfigStore *configs, QObject *parent = nullptr);
    QString language() const;
    void setLanguage(const QString &language);
    Q_INVOKABLE QString tr(const QString &context, const QString &sourceText) const;
    Q_INVOKABLE QString getSystemLanguage() const;
signals:
    void languageChanged(const QString &language);

private:
    const ConfigStore *m_configs;
};

// 对应 core/notification/manager.py —— M4 落地；dynamicNotification.qml / FloatingWidget.qml
// 连接 notified(payload)，payload 为 {title, message, icon, level} 形状的 QVariantMap
class NotificationStub : public QObject
{
    Q_OBJECT
public:
    explicit NotificationStub(QObject *parent = nullptr);
signals:
    void notifyQmlReady();
    void notified(const QVariantMap &payload);
};

// 对应 core/schedule/runtime.py ScheduleRuntime —— M2 落地（4 个活动类小组件取数源）。
// 属性形状按 QML 消费面给出安全空值（widget 内部均有 || 默认值兜底）：
//   currentStatus: "free"/"break"/"class"/"activity"/...（空串 → "Nothing right now" 分支）
//   currentSubject: {name, color, icon, isLocalClassroom}
//   currentEntry:   {title, ...}
//   remainingTime:  {minute, second}
//   nextEntries / subjects: 数组
class ScheduleRuntimeStub : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentStatus READ currentStatus NOTIFY scheduleChanged)
    Q_PROPERTY(QVariantMap currentSubject READ currentSubject NOTIFY scheduleChanged)
    Q_PROPERTY(QVariantMap currentEntry READ currentEntry NOTIFY scheduleChanged)
    Q_PROPERTY(QVariantMap remainingTime READ remainingTime NOTIFY scheduleChanged)
    Q_PROPERTY(QVariantList nextEntries READ nextEntries NOTIFY scheduleChanged)
    Q_PROPERTY(QVariantList subjects READ subjects NOTIFY scheduleChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY scheduleChanged)
public:
    explicit ScheduleRuntimeStub(QObject *parent = nullptr);

    QString currentStatus() const { return {}; }
    QVariantMap currentSubject() const
    {
        return { { "name", QString() }, { "color", QString() },
                 { "icon", QString() }, { "isLocalClassroom", false } };
    }
    QVariantMap currentEntry() const { return { { "title", QString() } }; }
    QVariantMap remainingTime() const { return { { "minute", 0 }, { "second", 0 } }; }
    QVariantList nextEntries() const { return {}; }
    QVariantList subjects() const { return {}; }
    qreal progress() const { return 0.0; }
signals:
    void scheduleChanged();
};

// 对应 core/schedule/manager.py ScheduleManager —— M2 落地；TrayPanel.qml 引用三个成员
class ScheduleManagerStub : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList schedules READ schedules NOTIFY schedulesChanged)
    Q_PROPERTY(QString currentScheduleName READ currentScheduleName NOTIFY currentScheduleNameChanged)
public:
    explicit ScheduleManagerStub(const ConfigStore *configs, QObject *parent = nullptr);
    // TrayPanel.qml:96 以函数形式调用 schedules()
    Q_INVOKABLE QVariantList schedules() const { return m_schedules; }
    QString currentScheduleName() const;
    Q_INVOKABLE void load(const QString &name);
signals:
    void schedulesChanged();
    void currentScheduleNameChanged();

private:
    const ConfigStore *m_configs;
    QVariantList m_schedules;
};

// 对应 core/schedule/editor.py ScheduleEditor —— M2 落地（编辑器窗口后端）；
// AppCentral.scheduleEditor 属性的占位，M1 加载的 QML 未引用
class ScheduleEditorStub : public QObject
{
    Q_OBJECT
public:
    explicit ScheduleEditorStub(QObject *parent = nullptr);
};

// 对应 core/windows/manager.py AppWindowManager —— M3 落地（10 类窗口工厂）
class WindowManagerStub : public QObject
{
    Q_OBJECT
public:
    explicit WindowManagerStub(QObject *parent = nullptr);
    Q_INVOKABLE void openSettings();
    Q_INVOKABLE void openEditor();
    Q_INVOKABLE void openPluginPlaza();
    Q_INVOKABLE void openWhatsNew();
    Q_INVOKABLE void openTutorial();
    Q_INVOKABLE void openDebugger();
    Q_INVOKABLE void openClassSwap();
    Q_INVOKABLE void openSingleInstanceDialog();
};

// 对应 core/schedule/swapper.py ClassSwapManager —— M2 落地；M1 仅注册名字防绑定缺失
class ClassSwapManagerStub : public QObject
{
    Q_OBJECT
public:
    explicit ClassSwapManagerStub(QObject *parent = nullptr);
};

// 对应 core/utils/backend.py UtilsBackend —— M4 落地。
// TrayShortcuts.qml（随 TrayPanel 在 M1 启动）读取 shortcuts/availableShortcuts/allShortcuts
// 并调用 executeShortcut/moveShortcutTo/setShortcutEnabled；空列表 → 面板显示空态，无报错。
class UtilsBackendStub : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList shortcuts READ shortcuts NOTIFY shortcutsChanged)
    Q_PROPERTY(QVariantList availableShortcuts READ availableShortcuts NOTIFY shortcutsChanged)
    Q_PROPERTY(QVariantList allShortcuts READ allShortcuts NOTIFY shortcutsChanged)
public:
    explicit UtilsBackendStub(QObject *parent = nullptr);

    QVariantList shortcuts() const { return {}; }
    QVariantList availableShortcuts() const { return {}; }
    QVariantList allShortcuts() const { return {}; }

    Q_INVOKABLE bool executeShortcut(const QString &shortcutId);
    Q_INVOKABLE void moveShortcutTo(const QString &shortcutId, int index);
    Q_INVOKABLE void setShortcutEnabled(const QString &shortcutId, bool enabled);
signals:
    void shortcutsChanged();
};

// 对应 core/plugin/manager.py PluginManager —— Phase 2；M1 仅注册名字
// （插件专属 QML 页面不参与 M1 构建，见移植方案 §0.5.8）
class PluginManagerStub : public QObject
{
    Q_OBJECT
public:
    explicit PluginManagerStub(QObject *parent = nullptr);
};
