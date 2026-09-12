#include "UpdateDownloader.h"

#include "../ConfigStore.h"
#include "../Logger.h"

#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>

using cwn::Log;

namespace {

// downloader.py:32 的三重条件读取辅助（ConfigStore::value 缺键返回 nullopt）
std::optional<QJsonValue> configValue(const ConfigStore *configs, const char *key)
{
    return configs ? configs->value(QString::fromLatin1(key)) : std::nullopt;
}

} // namespace

UpdateDownloader::UpdateDownloader(const QString &url, const QString &destPath,
                                   ConfigStore *configs, QObject *parent)
    : QObject(parent)
    , m_configs(configs)
    , m_nam(new QNetworkAccessManager(this))
    , m_url(url)
    , m_dest(destPath)
{
}

UpdateDownloader::~UpdateDownloader()
{
    // 析构时先断开本对象的信号连接，避免 abort 触发的 finished 处理
    // 在销毁过程中再操作本对象
    if (m_reply) {
        m_reply->disconnect(this);
    }
    abortReply();
}

void UpdateDownloader::stop(bool manual)
{
    // downloader.py:20-24：置停止标记；manual=true 额外标记手动中断
    m_stopFlag = true;
    if (manual) {
        m_manualStop = true;
    }
    abortReply();
}

QString UpdateDownloader::tempUpdateDir()
{
    // bridge.py:28 Path(tempfile.gettempdir()) / "cwn_update"
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation)
        + QStringLiteral("/cwn_update");
}

QString UpdateDownloader::resolveUrl(const QString &url) const
{
    // downloader.py:26-36 _resolve_url
    const QUrl parsed(url);
    if (!parsed.host().contains(QLatin1String("github.com"))) {
        return url; // downloader.py:29-31：非 GitHub 直链不走镜像
    }

    // downloader.py:32：mirrors 非空 且 current_mirror 非空 且 mirror_enabled
    const auto mirrors = configValue(m_configs, "network.mirrors");
    const auto currentMirror = configValue(m_configs, "network.current_mirror");
    const auto mirrorEnabled = configValue(m_configs, "network.mirror_enabled");
    if (!mirrors.has_value() || !currentMirror.has_value() || !mirrorEnabled.has_value()
        || !mirrorEnabled->toBool()) {
        return url; // downloader.py:36 else return url
    }

    const QString mirrorName = currentMirror->toString();
    const QString mirror = mirrors->toObject().value(mirrorName).toString();
    if (mirror.isEmpty()) {
        // 上游此处 KeyError（被 DownloadWorker 捕获为失败）；本移植保守回退直链并告警
        Log::warn(QStringLiteral("Mirror '%1' not found in network.mirrors, using direct URL")
                      .arg(mirrorName));
        return url;
    }
    // downloader.py:34：镜像前缀 + 原 URL（字符串拼接，镜像自带尾部 "/"）
    return mirror + url;
}

void UpdateDownloader::start()
{
    m_stopFlag = false; // downloader.py:41：每次 download() 重置停止标记
    m_manualStop = false;
    m_finishedEmitted = false;
    m_downloaded = 0;

    const QString resolvedUrl = resolveUrl(m_url);
    // downloader.py:43
    Log::info(QStringLiteral("Downloading updates from: %1").arg(resolvedUrl));

    // downloader.py:45：requests.get(resolved_url, stream=True, proxies=None) 无超时；
    // 本移植同样不设传输超时（对齐上游语义，中断依赖 stop()）
    QNetworkRequest request(resolvedUrl);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    // downloader.py:50 open(self.dest, 'wb')：截断旧文件（readyRead 阶段按 Append 追加）
    QFile::remove(m_dest);

    m_reply = m_nam->get(request);
    m_elapsed.start(); // downloader.py:49 start_time

    connect(m_reply, &QNetworkReply::readyRead, this, [this] {
        if (!m_reply) {
            return;
        }
        if (m_stopFlag) {
            // downloader.py:52-53：检测到停止标记立即中断（→ finished(false, ..., manual)）
            abortReply();
            return;
        }
        // downloader.py:54 f.write(chunk)：iter_content(8192) 的逐块写等价实现
        const QByteArray chunk = m_reply->readAll();
        if (chunk.isEmpty()) {
            return;
        }
        QFile file(m_dest);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) {
            Log::error(QStringLiteral("Failed to open download target: %1").arg(m_dest));
            abortReply();
            return;
        }
        file.write(chunk);
        file.close(); // 逐块开关文件较慢但最稳（避免长期占用句柄）；块通常 8-64KB，开销可忽略
        m_downloaded += chunk.size();
    });

    // downloader.py:47/59：content-length 与进度上报。
    // total 未知（-1，分块传输）时上游会 ZeroDivisionError 失败，本移植保守保持 0%
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        if (!m_reply || m_stopFlag) {
            return;
        }
        // downloader.py:56-58：速度 = downloaded / elapsed（下限 0.001s）
        const double elapsed = qMax(m_elapsed.elapsed() / 1000.0, 0.001);
        const double speed = m_downloaded / elapsed;
        const double percent = total > 0 ? (received * 100.0 / total) : 0.0;
        emit progressChanged(percent, speed);
    });

    connect(m_reply, &QNetworkReply::finished, this, [this] {
        if (m_finishedEmitted || !m_reply) {
            return;
        }
        m_finishedEmitted = true;
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        reply->deleteLater();

        if (m_stopFlag) {
            // workers.py:66：success=False, "Download cancelled or failed.", manual_stop
            emit finished(false, QStringLiteral("Download cancelled or failed."), m_manualStop);
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            // workers.py:68 except Exception as e → finished(False, str(e), False)
            emit finished(false, reply->errorString(), false);
            return;
        }
        const QVariant statusCode =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        if (statusCode.isValid() && statusCode.toInt() >= 400) {
            // downloader.py:46 raise_for_status → worker 捕获为错误
            emit finished(false,
                          QStringLiteral("HTTP error: %1").arg(statusCode.toInt()), false);
            return;
        }
        emit finished(true, QString(), false); // workers.py:64
    });
}

void UpdateDownloader::abortReply()
{
    if (!m_reply) {
        return;
    }
    // abort() 通常同步触发 finished → 上面的 finished 处理把 m_reply 置空并
    // 发出 manualStop 语义的 finished 信号；若 finished 早已发过（处理提前返回），
    // 则在此兜底回收 reply 对象
    QNetworkReply *reply = m_reply;
    reply->abort();
    if (m_reply == reply) {
        m_reply = nullptr;
        reply->deleteLater();
    }
}
