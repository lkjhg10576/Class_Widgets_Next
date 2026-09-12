#include "UtilsBackend.h"

#include "../AppPaths.h"
#include "../ConfigStore.h"
#include "../Logger.h"
#include "AutoStartup.h"
#include "LogListModel.h"
#include "notification/NotificationService.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonValue>
#include <QMetaObject>
#include <QProcess>
#include <QTime>
#include <optional>

namespace cwn {
namespace utils {

UtilsBackend *UtilsBackend::s_instance = nullptr;
QtMessageHandler UtilsBackend::s_previousHandler = nullptr;

namespace {

// backend.py:92 record["level"].name —— Qt 消息级别 → loguru 风格级别名。
//（loguru 的 SUCCESS 级别无 Qt 对应，Dashboard.qml:127 的 SUCCESS 分支仅作
//  展示兜底，不会由本桥产生）
QString qtLevelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return QStringLiteral("DEBUG");
    case QtInfoMsg:
        return QStringLiteral("INFO");
    case QtWarningMsg:
        return QStringLiteral("WARNING");
    case QtCriticalMsg:
    case QtFatalMsg:
        return QStringLiteral("ERROR");
    }
    return QStringLiteral("INFO");
}

} // namespace

UtilsBackend::UtilsBackend(ConfigStore *configs, QObject *parent)
    : QObject(parent)
    , m_configs(configs)
{
    // backend.py:43 _register_debug_provider 为调试通知 Provider 注册（属通知域，
    // NotificationProvider.h 不在本任务边界内）；Debugger/contents/Overview.qml:44
    // 对 debugNotificationProvider 有判空保护，暂返回 nullptr 安全降级。C++ 侧
    // 的对应注册建议由通知域在 M4 联调时落地。

    // central.py:142-170：内置托盘快捷方式（上游在 _initialize_utils 中先于
    // UtilsBackend 构造注册，此处合并进构造）
    registerBuiltinShortcuts();

    // backend.py:35-37：日志模型 + 过滤代理（logs 属性暴露 proxy）
    m_logModel = new LogListModel(this);
    m_logProxy = new LogFilterProxyModel(this);
    m_logProxy->setSourceModel(m_logModel);
    // backend.py:27/38 _log_appended 信号 → GUI 线程 append_entry；消息处理器
    // 可能在任意线程触发，故用队列连接 marshal
    connect(this, &UtilsBackend::logArrived, m_logModel, &LogListModel::appendEntry,
            Qt::QueuedConnection);

    // backend.py:41 configs.configChanged → shortcutsChanged（配置被改即刷新列表）
    if (m_configs) {
        connect(m_configs, &ConfigStore::dataChanged, this,
                [this] { emit shortcutsChanged(); });
    }

    // backend.py:39-40 插件页注册/插件 shortcutsChanged 的连接属插件体系
    //（Phase 2）：extraSettings 恒为空列表、插件快捷方式暂无，QML 均有空态兜底。

    // backend.py:53 load_license：启动时立即加载
    loadLicense();

    // backend.py:52/80-95 _init_logger：挂钩日志 → QML Debugger
    attachLogCapture();
}

UtilsBackend::~UtilsBackend()
{
    if (s_instance == this) {
        s_instance = nullptr;
        if (s_previousHandler) {
            qInstallMessageHandler(s_previousHandler); // 还原 Logger 的处理器
            s_previousHandler = nullptr;
        }
    }
}

void UtilsBackend::setNotificationService(NotificationService *service)
{
    m_notifications = service;
    if (service) {
        // 设置页对 provider 列表变化的感知（上游 QML 轮询重读，这里补信号联动）
        connect(service, &NotificationService::notificationProvidersChanged, this,
                &UtilsBackend::notificationProvidersChanged, Qt::UniqueConnection);
    }
}

void UtilsBackend::setWindowManager(QObject *manager)
{
    m_windowManager = manager;
}

// ── 属性（backend.py:102-163 / 216-225 / 311-316）────────────────

QObject *UtilsBackend::logs() const
{
    // backend.py:102-107：暴露 LogFilterProxyModel（constant，对象本身不变）
    return m_logProxy;
}

QObject *UtilsBackend::debugNotificationProvider() const
{
    // backend.py:220-225 —— C++ 侧暂未注册调试 Provider（见构造函数注释），
    // 返回 nullptr；Overview.qml:44-53 的 if (provider) 分支安全降级
    return nullptr;
}

QVariantList UtilsBackend::extraSettings() const
{
    // backend.py:142-144 plugin_api.ui.pages —— 插件设置页，Phase 2 落地；
    // Settings.qml:81 对空列表显示为无子项
    return {};
}

QVariantList UtilsBackend::shortcuts() const
{
    // backend.py:146-150：按配置顺序输出已注册的快捷方式
    QVariantList out;
    const QStringList configured = configuredShortcuts();
    for (const QString &id : configured) {
        for (const ShortcutEntry &entry : m_registry) {
            if (entry.id == id) {
                out.append(shortcutPayload(entry));
                break;
            }
        }
    }
    return out;
}

QVariantList UtilsBackend::availableShortcuts() const
{
    // backend.py:152-159：已注册但未加入配置的快捷方式（"添加"对话框数据源）
    const QStringList configured = configuredShortcuts();
    QVariantList out;
    for (const ShortcutEntry &entry : m_registry) {
        if (!configured.contains(entry.id))
            out.append(shortcutPayload(entry));
    }
    return out;
}

QVariantList UtilsBackend::allShortcuts() const
{
    // backend.py:161-163
    QVariantList out;
    out.reserve(m_registry.size());
    for (const ShortcutEntry &entry : m_registry)
        out.append(shortcutPayload(entry));
    return out;
}

QString UtilsBackend::licenseText() const
{
    // backend.py:216-218
    return m_licenseText;
}

QVariantList UtilsBackend::notificationProviders() const
{
    // backend.py:311-316 → notification_service.notificationProviders
    return m_notifications ? m_notifications->notificationProviders() : QVariantList {};
}

// ── 槽 ──────────────────────────────────────────────────────────

void UtilsBackend::retranslate()
{
    // central.py:179-185：名称在 shortcutPayload 取值时即时翻译，
    // 重发 shortcutsChanged 即完成内置快捷方式名称的 retranslate
    emit shortcutsChanged();
}

void UtilsBackend::setLogFilterText(const QString &text)
{
    // backend.py:109-112
    m_logProxy->setFilterText(text);
}

void UtilsBackend::setLogFilterLevel(const QString &level)
{
    // backend.py:114-117
    m_logProxy->setFilterLevel(level);
}

QVariantList UtilsBackend::clearLogs()
{
    // backend.py:122-139：删除 logs/ 下全部文件，返回 [成功, KB(两位小数)]
    double kb = 0.0;
    const QDir dir(AppPaths::instance().logsRoot());
    if (dir.exists()) {
        QDirIterator it(dir.absolutePath(), QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            const QFileInfo info = it.fileInfo();
            kb += info.size() / 1024.0;
            if (!QFile::remove(it.filePath())) {
                // backend.py:135 PermissionError → 跳过该文件继续
                cwn::Log::debug(QStringLiteral("Permission denied: %1").arg(info.fileName()));
            }
        }
    }
    // backend.py:136 round(size, 2)
    return { true, qRound(kb * 100.0) / 100.0 };
}

bool UtilsBackend::executeShortcut(const QString &shortcutId)
{
    // backend.py:165-167 → plugin_api.ui.invoke_shortcut（components.py:480-492）。
    // C++ 侧动作直接映射 central.py:144-170 注册的内置回调。
    if (shortcutId == QLatin1String("com.classwidgets.reschedule-day")) {
        // central.py:175-177 _request_reschedule_day_shortcut：转发给 AppCentral
        //（集成时连接本信号与 AppCentral::trayShortcutRequested），返回 False
        // 使托盘面板保持打开（components.py:488 action() is not False）
        emit trayShortcutRequested(shortcutId);
        return false;
    }
    if (shortcutId == QLatin1String("com.classwidgets.settings"))
        return invokeWindowManager("openSettings");
    if (shortcutId == QLatin1String("com.classwidgets.schedules"))
        return invokeWindowManager("openEditor");
    if (shortcutId == QLatin1String("com.classwidgets.plugin-plaza"))
        return invokeWindowManager("openPluginPlaza");
    if (shortcutId == QLatin1String("com.classwidgets.class-swap"))
        return invokeWindowManager("openClassSwap");

    // components.py:484-485
    cwn::Log::warn(QStringLiteral("Shortcut action not found: %1").arg(shortcutId));
    return false;
}

bool UtilsBackend::setShortcutEnabled(const QString &shortcutId, bool enabled)
{
    // backend.py:169-185
    const QString configKey = QStringLiteral("preferences.shortcuts");
    if (m_configs && m_configs->isKeyLocked(configKey)) {
        // backend.py:172-174
        cwn::Log::warn(
            QStringLiteral("Attempt to modify locked config key: %1. Blocked.").arg(configKey));
        return false;
    }

    QStringList shortcuts = configuredShortcuts();
    if (enabled && !shortcuts.contains(shortcutId)) {
        shortcuts.append(shortcutId); // backend.py:177-178
    } else if (!enabled && shortcuts.contains(shortcutId)) {
        shortcuts.removeAll(shortcutId); // backend.py:179-180
    } else {
        return true; // backend.py:181-182：无变化视为成功
    }

    writeConfiguredShortcuts(shortcuts); // backend.py:184
    return true;
}

bool UtilsBackend::moveShortcutTo(const QString &shortcutId, int targetIndex)
{
    // backend.py:187-213：把已启用的快捷方式移动到可见顺序的 target_index
    const QString configKey = QStringLiteral("preferences.shortcuts");
    if (m_configs && m_configs->isKeyLocked(configKey)) // backend.py:191-192
        return false;

    const QStringList shortcuts = configuredShortcuts();
    QStringList registeredIds;
    registeredIds.reserve(m_registry.size());
    for (const ShortcutEntry &entry : m_registry)
        registeredIds.append(entry.id);

    QStringList visibleIds;
    for (const QString &id : shortcuts) {
        if (registeredIds.contains(id))
            visibleIds.append(id); // backend.py:196
    }

    const int sourceIndex = visibleIds.indexOf(shortcutId);
    if (sourceIndex < 0)
        return false; // backend.py:197-200

    targetIndex = qBound(0, targetIndex, visibleIds.size() - 1); // backend.py:202
    if (sourceIndex == targetIndex)
        return true; // backend.py:203-204

    const QString targetId = visibleIds.at(targetIndex); // backend.py:206
    QStringList updated = shortcuts;
    updated.removeAt(updated.indexOf(shortcutId));       // backend.py:207
    int targetPosition = updated.indexOf(targetId);      // backend.py:208
    if (targetIndex > sourceIndex)
        targetPosition += 1;                             // backend.py:209-210
    updated.insert(targetPosition, shortcutId);          // backend.py:211
    writeConfiguredShortcuts(updated);                   // backend.py:212
    return true;
}

bool UtilsBackend::copyToClipboard(const QString &text)
{
    // backend.py:241-249
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        cwn::Log::error(QStringLiteral("Failed to copy to clipboard: no clipboard"));
        return false;
    }
    clipboard->setText(text);
    return true;
}

bool UtilsBackend::autostartSupported()
{
    // backend.py:252-254 @Property(bool, constant=True) autostartSupported
    return AutoStartup::supported();
}

bool UtilsBackend::setAutostart(bool enabled)
{
    // backend.py:256-262：执行开关后回读实际状态
    AutoStartup::setEnabled(enabled);
    return AutoStartup::isEnabled();
}

bool UtilsBackend::autostartEnabled()
{
    // backend.py:264-266
    return AutoStartup::supported() && AutoStartup::isEnabled();
}

bool UtilsBackend::createDesktopShortcut()
{
    // backend.py:268-307：打包版在桌面创建 .lnk（PowerShell WScript.Shell）。
    // C++ 构建恒为"打包"形态（无脚本解释模式），对应 sys.frozen 分支恒真。
#ifdef Q_OS_WIN
    const QString target = QCoreApplication::applicationFilePath();
    if (target.isEmpty() || !QFileInfo::exists(target)) {
        cwn::Log::warn(QStringLiteral("Desktop shortcut is only available for packaged builds."));
        return false;
    }

    // backend.py:279-287 的脚本原文
    const QString script = QStringLiteral(
        "$shell = New-Object -ComObject WScript.Shell; "
        "$desktop = [Environment]::GetFolderPath('Desktop'); "
        "$shortcut = $shell.CreateShortcut((Join-Path $desktop 'Class Widgets Next.lnk')); "
        "$shortcut.TargetPath = $env:CW2_SHORTCUT_TARGET; "
        "$shortcut.WorkingDirectory = $env:CW2_SHORTCUT_WORKDIR; "
        "$shortcut.IconLocation = $env:CW2_SHORTCUT_ICON; "
        "$shortcut.Save()");

    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("CW2_SHORTCUT_TARGET"), target);
    environment.insert(QStringLiteral("CW2_SHORTCUT_WORKDIR"),
                       QCoreApplication::applicationDirPath());
    environment.insert(QStringLiteral("CW2_SHORTCUT_ICON"), target + QStringLiteral(",0"));
    process.setProcessEnvironment(environment);
    process.setProgram(QStringLiteral("powershell"));
    process.setArguments({ QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                           QStringLiteral("-Command"), script });
    process.start();
    if (!process.waitForFinished(30000) || process.exitCode() != 0) {
        // backend.py:305-307
        cwn::Log::error(QStringLiteral("Failed to create desktop shortcut: %1")
                            .arg(QString::fromUtf8(process.readAllStandardError())));
        return false;
    }
    return true;
#else
    // backend.py:271-272 platform.system() != "Windows"
    return false;
#endif
}

// ── 通知设置委托（backend.py:311-463 → NotificationService 同名方法）─────

void UtilsBackend::setNotificationProviderEnabled(const QString &providerId, bool enabled)
{
    if (m_notifications) // backend.py:318-323
        m_notifications->setNotificationProviderEnabled(providerId, enabled);
}

void UtilsBackend::setNotificationProviderSystemNotify(const QString &providerId, bool useSystem)
{
    if (m_notifications) // backend.py:325-330
        m_notifications->setNotificationProviderSystemNotify(providerId, useSystem);
}

void UtilsBackend::setNotificationProviderAppNotify(const QString &providerId, bool useApp)
{
    if (m_notifications) // backend.py:332-337
        m_notifications->setNotificationProviderAppNotify(providerId, useApp);
}

void UtilsBackend::setLevelSound(int level, const QString &sound)
{
    if (m_notifications) // backend.py:339-344
        m_notifications->setLevelSound(level, sound);
}

QString UtilsBackend::getLevelSound(int level) const
{
    // backend.py:346-351
    return m_notifications ? m_notifications->getLevelSound(level) : QString();
}

void UtilsBackend::setGlobalVolume(double volume)
{
    if (m_notifications) // backend.py:435-440（生效定义）
        m_notifications->setGlobalVolume(volume);
}

double UtilsBackend::getGlobalVolume() const
{
    // backend.py:427-433（生效定义）
    return m_notifications ? m_notifications->getGlobalVolume() : 0.0;
}

bool UtilsBackend::getNotificationsEnabled() const
{
    // backend.py:368-373
    return m_notifications ? m_notifications->getNotificationsEnabled() : false;
}

void UtilsBackend::setNotificationsEnabled(bool enabled)
{
    if (m_notifications) // backend.py:375-380
        m_notifications->setNotificationsEnabled(enabled);
}

double UtilsBackend::getNotificationProviderVolume(const QString &providerId) const
{
    // backend.py:383-388：实现忽略 provider_id
    Q_UNUSED(providerId);
    return m_notifications ? m_notifications->getNotificationVolume() : 0.0;
}

void UtilsBackend::setNotificationProviderVolume(const QString &providerId, double volume)
{
    // backend.py:390-395：实现忽略 provider_id
    Q_UNUSED(providerId);
    if (m_notifications)
        m_notifications->setNotificationVolume(volume);
}

QString UtilsBackend::getNotificationProviderLevelSound(const QString &providerId,
                                                        int level) const
{
    // backend.py:398-403
    return m_notifications ? m_notifications->getNotificationProviderLevelSound(providerId, level)
                           : QString();
}

void UtilsBackend::setNotificationProviderLevelSound(const QString &providerId, int level,
                                                     const QString &sound)
{
    // backend.py:405-410
    if (m_notifications)
        m_notifications->setNotificationProviderLevelSound(providerId, level, sound);
}

QString UtilsBackend::getGlobalLevelSound(int level) const
{
    // backend.py:413-418
    return m_notifications ? m_notifications->getGlobalLevelSound(level) : QString();
}

void UtilsBackend::setGlobalLevelSound(int level, const QString &sound)
{
    // backend.py:420-425
    if (m_notifications)
        m_notifications->setGlobalLevelSound(level, sound);
}

void UtilsBackend::playNotificationSoundLevel(int level)
{
    // backend.py:443-448
    if (m_notifications)
        m_notifications->playNotificationSoundLevel(level);
}

void UtilsBackend::playNotificationSound(const QString &providerId, int level)
{
    // backend.py:450-456
    if (m_notifications)
        m_notifications->playNotificationSound(providerId, level);
}

bool UtilsBackend::selectNotificationSound(int level)
{
    // backend.py:458-463（返回类型差异见头文件注释）
    return m_notifications ? m_notifications->selectNotificationSound(level) : false;
}

// ── 私有辅助 ────────────────────────────────────────────────────

void UtilsBackend::registerBuiltinShortcuts()
{
    // central.py:142-170 _register_shortcuts 的 5 个内置项。图标：上游用
    // cwn_*.png，C++ 仓库 assets 为 cw2_*.png（Path→URI 的归一化语义见
    // components.py:389-400：绝对路径/含后缀 → URI + iconIsSource=true）。
    // 名称取 "Shortcuts" 上下文英文源文，取值时经 QCoreApplication::translate
    // 即时翻译（等价 central.py:179-185 的 set_shortcut_name）。
    AppPaths &paths = AppPaths::instance();
    m_registry.append({ QStringLiteral("com.classwidgets.settings"),
                        "Settings",
                        paths.images(QStringLiteral("icons/cw2_settings.png")),
                        true });
    m_registry.append({ QStringLiteral("com.classwidgets.schedules"),
                        "Schedules",
                        paths.images(QStringLiteral("icons/cw2_editor.png")),
                        true });
    m_registry.append({ QStringLiteral("com.classwidgets.plugin-plaza"),
                        "Plugin Plaza",
                        paths.images(QStringLiteral("icons/cw2_plugin.png")),
                        true });
    m_registry.append({ QStringLiteral("com.classwidgets.reschedule-day"),
                        "Reschedule Day",
                        QStringLiteral("ic_fluent_calendar_arrow_counterclockwise_20_regular"),
                        false });
    m_registry.append({ QStringLiteral("com.classwidgets.class-swap"),
                        "Class Swap",
                        QStringLiteral("ic_fluent_arrow_swap_20_regular"),
                        false });
}

void UtilsBackend::loadLicense()
{
    // backend.py:227-239：读 ROOT_PATH/LICENSE
    QFile file(AppPaths::instance().root() + QStringLiteral("/LICENSE"));
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_licenseText = QString::fromUtf8(file.readAll());
    } else {
        cwn::Log::error(QStringLiteral("Failed to load license: %1").arg(file.errorString()));
        m_licenseText = QStringLiteral("License file not found."); // backend.py:234
    }
    emit licenseLoaded(); // backend.py:239
}

void UtilsBackend::attachLogCapture()
{
    // backend.py:80-95 _init_logger/_capture_log 的 C++ 等价物：链式接管 Qt 消息
    // 处理器 —— 先回放 Logger 已装处理器（stderr + logs/ 文件），再构造条目经
    // logArrived 信号（队列连接）投递到 GUI 线程的 LogListModel。
    if (s_instance)
        return; // UtilsBackend 由 AppCentral 单例装配，防御性保护
    s_instance = this;
    s_previousHandler = qInstallMessageHandler(&UtilsBackend::messageDispatch);
}

void UtilsBackend::messageDispatch(QtMsgType type, const QMessageLogContext &context,
                                   const QString &message)
{
    if (s_instance) {
        // backend.py:84-95：仅构造条目并经信号发回 GUI 线程，不直接改模型
        QVariantMap entry;
        entry.insert(QStringLiteral("time"),
                     QTime::currentTime().toString(QStringLiteral("HH:mm:ss")));
        entry.insert(QStringLiteral("level"), qtLevelName(type));
        entry.insert(QStringLiteral("message"), message);
        emit s_instance->logArrived(entry);
    }
    if (s_previousHandler)
        s_previousHandler(type, context, message); // 保持 Logger 的 stderr/文件输出
}

QStringList UtilsBackend::configuredShortcuts() const
{
    // configs.preferences.shortcuts（model.py:166-172，ConfigStore 默认树已含 5 项）
    QStringList out;
    if (!m_configs)
        return out;
    const auto value = m_configs->value(QStringLiteral("preferences.shortcuts"));
    const QJsonArray array = value.value_or(QJsonValue()).toArray();
    out.reserve(array.size());
    for (const QJsonValue &item : array) {
        const QString id = item.toString();
        if (!id.isEmpty())
            out.append(id);
    }
    return out;
}

bool UtilsBackend::writeConfiguredShortcuts(const QStringList &ids)
{
    // backend.py:184/212 self.app.configs.preferences.shortcuts = shortcuts
    if (!m_configs)
        return false;
    QJsonArray array;
    for (const QString &id : ids)
        array.append(id);
    m_configs->setInternal(QStringLiteral("preferences.shortcuts"), array);
    return true;
}

QVariantMap UtilsBackend::shortcutPayload(const ShortcutEntry &entry) const
{
    // components.py:420-425：{id, name, icon, iconIsSource, owner}
    return {
        { QStringLiteral("id"), entry.id },
        { QStringLiteral("name"),
          QCoreApplication::translate("Shortcuts", entry.nameSource) },
        { QStringLiteral("icon"), entry.icon },
        { QStringLiteral("iconIsSource"), entry.iconIsSource },
        { QStringLiteral("owner"), entry.owner },
    };
}

bool UtilsBackend::invokeWindowManager(const char *method)
{
    // window_manager 回调（central.py:148/154/160/168）：窗口管理器未注入或
    // 方法缺失时与 components.py:481-486 同语义告警并返回 False
    if (!m_windowManager) {
        cwn::Log::warn(QStringLiteral("Shortcut action not found: window manager not attached"));
        return false;
    }
    return QMetaObject::invokeMethod(m_windowManager, method, Qt::DirectConnection);
}

} // namespace utils
} // namespace cwn
