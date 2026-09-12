#include "UpdaterBridge.h"

#include "UpdateDownloader.h"
#include "../ConfigStore.h"
#include "../Logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Log = cwn::Log; // 命名空间别名：MSVC 拒绝 using cwn::Log;（C2873）

namespace {

// workers.py:11 UPDATE_URL（configs.network.releases_url 为空时的回退，workers.py:30-31）
constexpr char kFallbackReleasesUrl[] = "https://classwidgets.rinlit.cn/2/releases.json";

// updater.py:7 APP_NAME
constexpr wchar_t kAppExeName[] = L"Class Widgets Next.exe";

// workers.py:41-42 platform.system().lower()：
//   "windows" / "darwin" / "linux"（releases.json 的 url 字段按此键取平台下载地址）
QString platformSystemLower()
{
#ifdef Q_OS_WIN
    return QStringLiteral("windows");
#elif defined(Q_OS_DARWIN)
    return QStringLiteral("darwin"); // Python platform.system() 返回 "Darwin"
#else
    return QStringLiteral("linux");
#endif
}

} // namespace

UpdaterBridge::UpdaterBridge(ConfigStore *configs, QObject *parent)
    : QObject(parent)
    , m_configs(configs)
    , m_nam(new QNetworkAccessManager(this))
{
}

QString UpdaterBridge::configString(const char *key) const
{
    if (!m_configs) {
        return {};
    }
    const std::optional<QJsonValue> v = m_configs->value(QString::fromLatin1(key));
    return v.has_value() ? v->toString() : QString();
}

void UpdaterBridge::setStatus(const QString &status)
{
    // bridge.py:58-62 _set_status：值不变不发信号
    if (m_status == status) {
        return;
    }
    m_status = status;
    emit statusChanged(status);
}

void UpdaterBridge::setProgress(double percent, double speed)
{
    // bridge.py:64-67 _set_progress：属性存钳制值，信号发原始值（QML 读属性）
    m_progress = qBound(0.0, percent, 100.0);
    m_speed = speed;
    emit progressChanged(percent, speed);
}

void UpdaterBridge::setError(const QString &message)
{
    // bridge.py:69-73 _set_error
    m_errorDetails = message;
    emit errorDetailsChanged(message);
    emit errorOccurred(message);
    Log::error(QStringLiteral("[UpdaterBridge] %1").arg(message));
}

void UpdaterBridge::updateComplete()
{
    // bridge.py:75-92 update_complete：清理临时目录
    const QString tempDir = UpdateDownloader::tempUpdateDir();
    QDir dir(tempDir);
    if (dir.exists()) {
        if (dir.removeRecursively()) {
            // bridge.py:80
            Log::info(QStringLiteral("Temporary directory removed."));
        } else {
            // bridge.py:84
            Log::warn(QStringLiteral("Failed to remove temporary directory"));
        }
    }

    // bridge.py:85-92："已更新到最新版" 通知（上游经 tray_icon.push_up_to_date_notification）
    const QString version = configString("app.version"); // 对应上游 src.__version__
    emit notificationRequested(
        QCoreApplication::translate("UpdateNotification", "Update Completed ヾ(≧▽≦*)o"),
        QCoreApplication::translate("UpdateNotification",
                                    "Class Widgets has been updated to the latest version: %1")
            .arg(version));
}

void UpdaterBridge::maybeNotifyUpdateComplete()
{
    // central.py:467-470："--update-done" in sys.argv（open_whatsnew 由主控侧处理）
    if (QCoreApplication::arguments().contains(QStringLiteral("--update-done"))) {
        updateComplete();
    }
}

// ────────────────────────── 检查更新 ──────────────────────────

void UpdaterBridge::checkUpdate()
{
    // bridge.py:94-106 checkUpdate
    setStatus(QStringLiteral("Checking")); // bridge.py:97
    // bridge.py:98-99：channel / current_version 均来自配置
    // （ConfigStore 保证 app.version/app.channel 与构建常量一致）
    m_checkChannel = configString("app.channel");
    m_checkCurrentVersion = configString("app.version");

    // bridge.py:101-102：终止进行中的检查（上游 terminate 工作线程 → 这里 abort 请求）
    if (m_checkReply) {
        QNetworkReply *reply = m_checkReply;
        m_checkReply = nullptr;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }

    // bridge.py:106：self._check_worker.start(self.app_central.configs.network.releases_url)
    QString url = configString("network.releases_url");
    if (url.isEmpty()) {
        url = QString::fromLatin1(kFallbackReleasesUrl); // workers.py:30-31
    }

    QNetworkRequest request(url);
    // workers.py:32 requests.get(self.url, timeout=5)：5 秒超时。
    // setTransferTimeout 为"无数据传输"超时，语义近似（保守且更宽容慢连接）
    request.setTransferTimeout(5000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    m_checkReply = m_nam->get(request);
    connect(m_checkReply, &QNetworkReply::finished, this, &UpdaterBridge::onCheckFinished);
}

void UpdaterBridge::onCheckFinished()
{
    // bridge.py:108-120 _on_check_finished（请求体逻辑来自 workers.py:28-49 run()）
    if (!m_checkReply) {
        return;
    }
    QNetworkReply *reply = m_checkReply;
    m_checkReply = nullptr;
    reply->deleteLater();

    // bridge.py:109-112：status == "Error" → Error + errorDetails
    auto fail = [this](const QString &message) {
        setStatus(QStringLiteral("Error"));
        setError(QStringLiteral("Check update failed: %1").arg(message));
    };

    if (reply->error() != QNetworkReply::NoError) {
        // workers.py:49 except Exception as e → finished("Error", "", str(e))
        fail(reply->errorString());
        return;
    }
    const QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (statusCode.isValid() && statusCode.toInt() >= 400) {
        // workers.py:33 raise_for_status
        fail(QStringLiteral("HTTP %1").arg(statusCode.toInt()));
        return;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        // workers.py:34 resp.json() 抛异常 → 捕获为 Error
        fail(parseError.errorString());
        return;
    }
    const QJsonObject data = doc.object();

    // workers.py:35-38：info = data.get(channel)；`if not info` 把缺失键与空对象
    // 都视为 "Missing channel info"
    const QJsonValue channelValue = data.value(m_checkChannel);
    if (!channelValue.isObject() || channelValue.toObject().isEmpty()) {
        fail(QStringLiteral("Missing channel info"));
        return;
    }
    const QJsonObject info = channelValue.toObject();

    // workers.py:40：version = info.get("version", "")
    const QString version = info.value(QStringLiteral("version")).toString();
    // workers.py:42：url = info.get("url", {}).get(sys_name, "") or ""
    const QString url =
        info.value(QStringLiteral("url")).toObject().value(platformSystemLower()).toString();

    // workers.py:44：版本比较语义 = 纯字符串不等（`version != self.current_version`），
    // 上游没有 semver / 数字段比较 —— 本移植保持逐字一致
    if (version != m_checkCurrentVersion) {
        // workers.py:45 → bridge.py:114-118
        m_latestVersion = version;
        m_latestUrl = url;
        setStatus(QStringLiteral("UpdateAvailable"));
        emit updateAvailable(version, url);
    } else {
        setStatus(QStringLiteral("UpToDate")); // workers.py:46-47 → bridge.py:120
    }
}

// ────────────────────────── 下载更新 ──────────────────────────

void UpdaterBridge::startDownload()
{
    // bridge.py:122-127：仅 Windows 支持下载
#ifndef Q_OS_WIN
    setStatus(QStringLiteral("UnsupportedPlatform"));
    return;
#else
    if (m_latestUrl.isEmpty()) {
        // bridge.py:129-132
        setStatus(QStringLiteral("Error"));
        setError(QStringLiteral("No download URL available."));
        return;
    }

    // bridge.py:134-140：终止上一次下载（非手动，manual_stop 保持 False）
    if (m_downloader) {
        m_downloader->stop(false);
        m_downloader->deleteLater();
        m_downloader = nullptr;
    }

    setStatus(QStringLiteral("Downloading")); // bridge.py:143

    // bridge.py:144-145：temp_dir.mkdir + update.zip
    const QString tempDir = UpdateDownloader::tempUpdateDir();
    QDir().mkpath(tempDir);
    m_downloadedFile = tempDir + QStringLiteral("/update.zip");

    // bridge.py:147-151：创建下载器并连接 progress/finished
    m_downloader = new UpdateDownloader(m_latestUrl, m_downloadedFile, m_configs, this);
    connect(m_downloader, &UpdateDownloader::progressChanged,
            this, &UpdaterBridge::onDownloadProgress);
    connect(m_downloader, &UpdateDownloader::finished,
            this, &UpdaterBridge::onDownloadFinished);
    m_downloader->start();
#endif
}

void UpdaterBridge::onDownloadProgress(double percent, double speed)
{
    setProgress(percent, speed); // bridge.py:149 progress → _set_progress
}

void UpdaterBridge::onDownloadFinished(bool success, const QString &message, bool manualStop)
{
    // bridge.py:153-171 _on_download_finished
    if (m_downloader) {
        m_downloader->deleteLater();
        m_downloader = nullptr;
    }

    if (!success) {
        if (manualStop) {
            setStatus(QStringLiteral("Idle")); // bridge.py:156（QML 的 "Cancelled" 分支不可达，同上游）
        } else {
            setStatus(QStringLiteral("Error")); // bridge.py:158
            setError(message);                  // bridge.py:159
        }
        return;
    }

    // bridge.py:162-168："更新已下载" 托盘通知
    emit notificationRequested(
        QCoreApplication::translate("UpdateNotification", "Update Downloaded"),
        QCoreApplication::translate(
            "UpdateNotification",
            "Ready to install anytime. Go to \"Settings\" → \"Update\" to proceed with installation."));

    setStatus(QStringLiteral("Downloaded"));  // bridge.py:170
    emit installReady(m_latestVersion);       // bridge.py:171
}

void UpdaterBridge::stopDownload()
{
    // bridge.py:173-183 stopDownload
    setProgress(0.0, 0.0); // bridge.py:175
    if (m_downloader) {
        // bridge.py:177 worker.stop(force=True) → downloader.stop(manual=True)：
        // finished 信号随后以 manualStop=true 到达 → 状态回落 Idle
        m_downloader->stop(true);
    }
}

// ────────────────────────── 安装更新 ──────────────────────────

void UpdaterBridge::startInstall()
{
    // bridge.py:185-199 startInstall
    const QFileInfo fileInfo(m_downloadedFile);
    if (m_downloadedFile.isEmpty() || !fileInfo.exists()) {
        // bridge.py:187-190
        setStatus(QStringLiteral("Error"));
        setError(QStringLiteral("Downloaded file not found."));
        return;
    }

    setStatus(QStringLiteral("Installing")); // bridge.py:192

    // bridge.py:193-197 InstallWorker(WindowsUpdater(temp_dir), zip, Path.cwd())：
    // 上游在工作线程解压；本移植同步执行（解压通常 1-3s，UI 已显示 Installing）。
    // 目标目录取可执行文件目录（比上游 Path.cwd() 更可靠）
    QString errorMessage;
    const bool ok = applyUpdateArchive(
        m_downloadedFile, QCoreApplication::applicationDirPath(), &errorMessage);

    if (ok) {
        setStatus(QStringLiteral("Installed")); // bridge.py:203
        // bridge.py:204-209："即将应用更新" 通知
        emit notificationRequested(
            QCoreApplication::translate("UpdateNotification", "Applying Update Soon"),
            QCoreApplication::translate(
                "UpdateNotification",
                "The update may take several seconds to complete. (●'◡'●)"));
        // bridge.py:210 app_central.restart()：cmd 脚本 2 秒后自启新实例（--update-done），
        // 主控收到 restartRequested 后应保存并退出当前应用（对应 updater.py:43 os._exit(0)）
        emit restartRequested();
    } else {
        setStatus(QStringLiteral("Error"));                       // bridge.py:212
        setError(QStringLiteral("Install failed: %1").arg(errorMessage)); // bridge.py:213
    }
}

bool UpdaterBridge::applyUpdateArchive(const QString &zipPath, const QString &targetDir,
                                       QString *errorMessage)
{
    // 对应上游 src/core/updater/updater.py WindowsUpdater.apply_update（18-43 行）
#ifndef Q_OS_WIN
    Q_UNUSED(zipPath);
    Q_UNUSED(targetDir);
    if (errorMessage) {
        *errorMessage = QStringLiteral("Unsupported platform");
    }
    return false;
#else
    const QString tempDir = UpdateDownloader::tempUpdateDir();

    // updater.py:20-21 解压目录；先清空（上游 extractall 直接覆盖，旧包残留文件
    // 会被 xcopy 一并带入新版本目录，这里保守地先删再解）
    const QString extractDir = tempDir + QStringLiteral("/extracted");
    QDir(extractDir).removeRecursively();
    if (!QDir().mkpath(extractDir)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot create extract dir: %1").arg(extractDir);
        }
        return false;
    }

    // updater.py:22-23 zipfile.ZipFile.extractall 的替代：优先用系统自带 tar
    // （Windows 10 1803+ 内置 bsdtar，支持 zip）；失败再回退 PowerShell Expand-Archive
    auto runProcess = [](const QString &program, const QStringList &arguments,
                         int timeoutMs, QString *output) -> bool {
        QProcess process;
        process.setProgram(program);
        process.setArguments(arguments);
        process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
            // updater.py:40 CREATE_NO_WINDOW（静默执行）
            args->flags |= CREATE_NO_WINDOW;
        });
        process.start();
        if (!process.waitForStarted(5000) || !process.waitForFinished(timeoutMs)) {
            process.kill();
            if (output) {
                *output = QStringLiteral("process failed to start/finish: %1").arg(program);
            }
            return false;
        }
        if (output) {
            *output = QString::fromLocal8Bit(process.readAllStandardError());
        }
        return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    };

    QString processOutput;
    if (!runProcess(QStringLiteral("tar"),
                    {QStringLiteral("-xf"), zipPath, QStringLiteral("-C"), extractDir},
                    120000, &processOutput)) {
        // PowerShell 兜底（Windows 10+ 自带 Expand-Archive）
        const QString command = QStringLiteral(
            "Expand-Archive -LiteralPath '%1' -DestinationPath '%2' -Force")
            .arg(zipPath, extractDir);
        if (!runProcess(QStringLiteral("powershell.exe"),
                        {QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"),
                         QStringLiteral("Bypass"), QStringLiteral("-Command"), command},
                        300000, &processOutput)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Extraction failed: %1").arg(processOutput);
            }
            return false;
        }
    }

    // updater.py:25-36 写 replace_and_restart.cmd：
    //   timeout 2s → xcopy 替换 → start 新实例（--update-done）
    const QString cmdFile = tempDir + QStringLiteral("/replace_and_restart.cmd");
    const QString targetExe = targetDir + QLatin1Char('\\') + QString::fromWCharArray(kAppExeName);
    const QString script = QStringLiteral(
        "@echo off\r\n"
        "timeout /t 2 /nobreak >nul\r\n"
        "echo Updating files...\r\n"
        "xcopy /E /Y /Q \"%1\" \"%2\"\r\n"
        "echo Done. Restarting...\r\n"
        "start \"\" \"%3\" --update-done\r\n")
        .arg(extractDir, targetDir, targetExe);

    QFile cmd(cmdFile);
    // updater.py:26 用 encoding="utf-8" 写；本移植改写本地代码页（toLocal8Bit）：
    // cmd.exe 按控制台代码页解析批处理，中文 %TEMP% 路径（如 C:\Users\<中文名>\…）
    // 在 UTF-8 下会乱码导致 xcopy 失败，本地 ANSI/OEM 编码（GBK 等）可正确解析
    if (!cmd.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || cmd.write(script.toLocal8Bit()) < 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot write update script: %1").arg(cmdFile);
        }
        return false;
    }

    // updater.py:38-42 subprocess.Popen(["cmd", "/c", cmd_file], CREATE_NO_WINDOW)
    QProcess process;
    process.setProgram(QStringLiteral("cmd"));
    process.setArguments({QStringLiteral("/c"), cmdFile});
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW; // updater.py:40 静默执行
    });
    if (!process.startDetached()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot launch update script");
        }
        return false;
    }

    // updater.py:43 os._exit(0)：进程终止由主控监听 restartRequested() 后正常退出
    // （先落盘配置，比 os._exit 更安全）；xcopy 在脚本 2 秒延迟后执行
    return true;
#endif
}
