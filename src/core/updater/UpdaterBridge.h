#pragma once

#include <QObject>
#include <QString>

class ConfigStore;
class UpdateDownloader;
class QNetworkAccessManager;
class QNetworkReply;

// 对应上游 src/core/updater/bridge.py UpdaterBridge（215 行），QML 上下文名
// "UpdaterBridge"（app/src/qml 消费面：pages/settings/Update.qml 共 23 处）。
//
// 职责：检查更新（releases.json）→ 下载（镜像前缀，见 UpdateDownloader）→
// 安装（解压 + xcopy 替换 + 重启，对应 updater.py WindowsUpdater）。
// 状态机字符串与上游逐字一致（QML switch 直接消费）：
//   Idle / Checking / UpdateAvailable / UpToDate / Downloading / Downloaded /
//   Installing / Installed / Error / UnsupportedPlatform
//   （上游还有 "Cancelled" 分支但从不设置——stopDownload 后回落 "Idle"，
//     bridge.py:156，QML 分支保留但不可达，本移植保持一致）
//
// 与 AppCentral 的衔接（M4 规则：不改 AppCentral）：
//   - 通知：上游经 app_central.tray_icon.push_*（bridge.py:89/162/204），
//     本移植发 notificationRequested 信号，主控接线到托盘/通知系统；
//   - 重启：上游 bridge.py:210 app_central.restart() + updater.py:43 os._exit(0)，
//     本移植发 restartRequested 信号（cmd 脚本 2 秒后自启新实例，主控应退出应用）；
//   - 注册：主控在 setupQmlContext 里
//     rootContext()->setContextProperty("UpdaterBridge", bridge)。
class UpdaterBridge : public QObject
{
    Q_OBJECT
    // bridge.py:42-44 status 属性
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    // bridge.py:46-48 progress 属性（0-100，写入时已钳制）
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    // bridge.py:50-52 speed 属性（bytes/sec）
    Q_PROPERTY(double speed READ speed NOTIFY progressChanged)
    // bridge.py:54-56 errorDetails 属性
    Q_PROPERTY(QString errorDetails READ errorDetails NOTIFY errorDetailsChanged)

public:
    // bridge.py:23-40 __init__(app_central)：C++ 侧只注入 ConfigStore
    explicit UpdaterBridge(ConfigStore *configs, QObject *parent = nullptr);

    QString status() const { return m_status; }
    double progress() const { return m_progress; }
    double speed() const { return m_speed; }
    QString errorDetails() const { return m_errorDetails; }

    // bridge.py:75-92 update_complete()：清理临时目录 + "已更新到最新版"托盘通知。
    // 上游由 central.py:467-470 在启动参数含 --update-done 时调用。
    void updateComplete();
    // central.py:467 包装：仅当启动参数含 --update-done 时执行 updateComplete()
    // （open_whatsnew 由主控侧处理，见报告集成说明）
    void maybeNotifyUpdateComplete();

public slots:
    // bridge.py:94-106 checkUpdate()：检查更新（所有平台通用）
    Q_INVOKABLE void checkUpdate();
    // bridge.py:122-151 startDownload()：仅 Windows 支持下载
    Q_INVOKABLE void startDownload();
    // bridge.py:173-183 stopDownload()：取消下载
    Q_INVOKABLE void stopDownload();
    // bridge.py:185-199 startInstall()：解压并替换更新（updater.py）
    Q_INVOKABLE void startInstall();

signals:
    void statusChanged(const QString &status);    // bridge.py:16
    // bridge.py:17：注意上游 emit 的是未钳制的原始 percent（钳制只作用于属性）
    void progressChanged(double percent, double speed);
    void errorDetailsChanged(const QString &message); // bridge.py:18
    void updateAvailable(const QString &version, const QString &url); // bridge.py:19
    void installReady(const QString &version);        // bridge.py:20
    void errorOccurred(const QString &message);       // bridge.py:21

    // C++ 集成补充：上游 tray_icon.push_notification / push_up_to_date_notification /
    // push_update_notification（bridge.py:89、162、204；update_check.py:44），
    // 主控将本信号接到 TrayIcon/通知系统
    void notificationRequested(const QString &title, const QString &text);
    // bridge.py:210 app_central.restart() + updater.py:43 os._exit(0) 的等价物：
    // 安装脚本已以 2 秒延迟自启新实例，主控收到后应退出应用（先落盘配置）
    void restartRequested();

private slots:
    void onCheckFinished(); // bridge.py:108-120 _on_check_finished（含 workers.py:28-49 请求体）
    void onDownloadProgress(double percent, double speed); // bridge.py:64-67 _set_progress
    void onDownloadFinished(bool success, const QString &message, bool manualStop); // bridge.py:153-171

private:
    void setStatus(const QString &status); // bridge.py:58-62 _set_status
    void setProgress(double percent, double speed); // bridge.py:64-67
    void setError(const QString &message); // bridge.py:69-73 _set_error
    bool applyUpdateArchive(const QString &zipPath, const QString &targetDir,
                            QString *errorMessage); // updater.py:18-43 apply_update
    QString configString(const char *key) const;

    ConfigStore *m_configs = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_checkReply = nullptr; // bridge.py:34 _check_worker（线程→异步请求）
    UpdateDownloader *m_downloader = nullptr; // bridge.py:36 _downloader

    QString m_latestVersion;   // bridge.py:38 _latest_version
    QString m_latestUrl;       // bridge.py:39 _latest_url
    QString m_downloadedFile;  // bridge.py:40 _downloaded_file

    QString m_status = QStringLiteral("Idle"); // bridge.py:29 _status
    double m_progress = 0.0;                   // bridge.py:30 _progress
    double m_speed = 0.0;                      // bridge.py:31 _speed
    QString m_errorDetails;                    // bridge.py:32 _error_details

    // 进行中检查请求的上下文（bridge.py:104 CheckUpdateWorker(channel, current_version)）
    QString m_checkChannel;
    QString m_checkCurrentVersion;
};
