#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>

class ConfigStore;
class QNetworkAccessManager;
class QNetworkReply;

// 对应上游 src/core/updater/downloader.py UpdateDownloader（60 行）+
// workers.py DownloadWorker（workers.py:52-74）。
//
// requests.get(stream=True) + iter_content(8192) 的流式下载移植为
// QNetworkAccessManager 异步下载（readyRead 逐块落盘，语义等价）。
// 超时/重试语义照 workers.py：检查请求 5s 超时（workers.py:32）；
// 下载请求上游无超时（downloader.py:45）→ 本类同样不设传输超时；
// 上游无自动重试（失败直接报错，换镜像由用户在设置里选择）→ 同样不重试。
class UpdateDownloader : public QObject
{
    Q_OBJECT

public:
    explicit UpdateDownloader(const QString &url, const QString &destPath,
                              ConfigStore *configs, QObject *parent = nullptr);
    ~UpdateDownloader() override;

    // downloader.py:20-24 stop(manual)：外部中断下载。
    // manual=true 时 finished 信号带 manualStop=true（bridge 据此回落 Idle 而非 Error）
    void stop(bool manual = false);

    // 下载临时目录：bridge.py:28 tempfile.gettempdir() / "cwn_update"。
    // 本移植走 Qt 路径层 QStandardPaths::TempLocation（与 gettempdir 等价；
    // AppPaths 运行时根可能位于 Program Files 下不可写，故不用 AppPaths::root()）。
    static QString tempUpdateDir();

public slots:
    // 对应 DownloadWorker.start() → run() → download()（workers.py:60-68 +
    // downloader.py:39-60）。异步发起，结果经 finished 报告。
    void start();

signals:
    // workers.py:53 progress Signal(float, float)：(百分比 0-100，速度 bytes/sec)
    // 速度语义照 downloader.py:56-57：downloaded / elapsed（elapsed 下限 0.001s）
    void progressChanged(double percent, double speedBytesPerSec);

    // workers.py:54 finished Signal(bool, str, bool)：(成功， 错误消息， 是否手动中断)
    void finished(bool success, const QString &message, bool manualStop);

private:
    // downloader.py:26-36 _resolve_url：仅当 URL 主机含 "github.com" 且
    // network.mirror_enabled 且 network.mirrors[network.current_mirror] 存在时，
    // 在原 URL 前拼接镜像前缀；否则原样返回。
    QString resolveUrl(const QString &url) const;

    void abortReply();

    ConfigStore *m_configs = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_reply = nullptr;
    QString m_url;       // downloader.py:13 原始 url
    QString m_dest;      // downloader.py:14 dest
    bool m_stopFlag = false;    // downloader.py:16
    bool m_manualStop = false;  // downloader.py:17
    bool m_finishedEmitted = false; // 保证 finished 只发一次（C++ 侧防重入）
    qint64 m_downloaded = 0;    // downloader.py:48 downloaded
    QElapsedTimer m_elapsed;    // downloader.py:49 start_time
};
