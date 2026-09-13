#include "ThemeUrlInterceptor.h"

#include "../AppPaths.h"
#include "../Logger.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>

#include <exception>

ThemeUrlInterceptor::ThemeUrlInterceptor(QObject *parent)
    : QObject(parent)
{
    // interceptor.py:15：毫秒时间戳 nonce
    m_nonce = makeNonce();
}

QString ThemeUrlInterceptor::makeNonce() const
{
    return QString::number(QDateTime::currentMSecsSinceEpoch());
}

QUrl ThemeUrlInterceptor::makeNonceUrl(const QString &localFile, const QString &fallback) const
{
    // B3 缓存纪律：cacheBuster 由"进程级时间戳"改为"解析后文件的 mtime+size 指纹"。
    //
    // 动机：时间戳让每次主题切换/重载把全部命中拦截的 URL 变成全新地址 ——
    // QQmlTypeLoader 的内存缓存与 QML 磁盘缓存（Qt 默认启用，键为拦截后的 URL）
    // 一律失效，等于"切一次主题 = 全量重编译"。
    //
    // 指纹语义：同一目标文件内容不变 → URL 恒定 → 缓存命中；内容变化（同路径被改写）
    // → mtime/size 变 → 仍然击穿。不同主题覆盖同一组件时目标路径本就不同，
    // 依旧满足 interceptor.py "新旧主题 URL 缓存互不污染" 的原意。
    //
    // stat 失败（文件不存在等）→ 退回调用方传入的进程级 nonce（保守：宁可多 bust 不可漏 bust）。
    QUrl result = QUrl::fromLocalFile(localFile);
    result.setQuery(QStringLiteral("t=") + fileFingerprint(localFile, fallback));
    return result;
}

QString ThemeUrlInterceptor::fileFingerprint(const QString &localFile, const QString &fallback)
{
    const QFileInfo info(localFile);
    if (!info.exists())
        return fallback;
    // '<mtime_ms>-<bytes>'：单调、稳定、无需哈希（成本 = 一次 stat）
    return QStringLiteral("%1-%2")
        .arg(info.lastModified().toMSecsSinceEpoch())
        .arg(info.size());
}

void ThemeUrlInterceptor::setThemePath(const QString &themePath)
{
    QMutexLocker locker(&m_mutex);

    // interceptor.py:20-23：空路径 → 解除拦截并重置 nonce
    if (themePath.isEmpty()) {
        m_currentThemePath.clear();
        m_nonce = makeNonce();
        return;
    }

    const QFileInfo info(themePath);
    if (info.exists() && info.isDir()) {
        // 统一分隔符便于 intercept() 里的前缀比较（interceptor.py:72）
        m_currentThemePath = QDir::fromNativeSeparators(themePath);
        m_nonce = makeNonce();
        cwn::Log::info(
            QStringLiteral("Theme interceptor set to: %1").arg(m_currentThemePath));
    } else {
        // interceptor.py:30-32：非法路径 → 告警并解除（nonce 不重置，忠实移植）
        cwn::Log::warn(QStringLiteral("Invalid theme path set: %1").arg(themePath));
        m_currentThemePath.clear();
    }
}

QUrl ThemeUrlInterceptor::intercept(const QUrl &url, DataType type)
{
    Q_UNUSED(type); // 上游同样不区分请求类型（interceptor.py:34）

    QMutexLocker locker(&m_mutex);

    if (m_currentThemePath.isEmpty())
        return url;

    // QUrl("file:///C:/path/to/file").toLocalFile() → "C:/path/to/file"
    QString source = url.toLocalFile();
    if (source.isEmpty())
        return url;
    source.replace(QLatin1Char('\\'), QLatin1Char('/')); // 统一分隔符（interceptor.py:48）

    const QString lowerSource = source.toLower();
    const QString lowerTarget = m_targetPath.toLower();
    const int idx = lowerSource.lastIndexOf(lowerTarget); // rfind：取最后一次出现
    if (idx < 0)
        return url;

    // 相对片段保留 "ClassWidgets/theme" 前缀本身
    const QString relativePart = source.mid(idx);

    // interceptor.py:64-69：基础主题模块的 qmldir 是 overlay——其描述文件通常只列出
    // 被覆盖的组件，替换它会把未覆盖的组件一并隐藏；嵌套模块（如
    // ClassWidgets.Theme.Material）是主题自有模块，必须解析自己的 qmldir。
    if (QFileInfo(source).fileName().compare(QLatin1String("qmldir"), Qt::CaseInsensitive) == 0
        && relativePart.compare(QLatin1String("ClassWidgets/theme/qmldir"),
                                Qt::CaseInsensitive) == 0) {
        return url;
    }

    // B3：m_nonce 仅作 stat 失败时的保守回退（持锁读取，避免指纹函数二次加锁）
    const QString fallback = m_nonce;

    // interceptor.py:71-82：防循环重定向——源路径已位于当前主题目录下
    if (lowerSource.startsWith(m_currentThemePath.toLower())) {
        if (QFileInfo::exists(source))
            return url;
        const QString defaultFile =
            AppPaths::instance().qmlRoot() + QLatin1Char('/') + relativePart;
        if (QFileInfo::exists(defaultFile))
            return makeNonceUrl(defaultFile, fallback);
        return url;
    }

    try {
        // interceptor.py:84-98：先找主题内文件，其次回退默认主题（src/qml）
        const QString targetFile = m_currentThemePath + QLatin1Char('/') + relativePart;
        if (QFileInfo::exists(targetFile))
            return makeNonceUrl(targetFile, fallback);

        const QString defaultFile =
            AppPaths::instance().qmlRoot() + QLatin1Char('/') + relativePart;
        if (QFileInfo::exists(defaultFile))
            return makeNonceUrl(defaultFile, fallback);
    } catch (const std::exception &e) {
        // interceptor.py:100-101：拦截异常只记录，不中断加载
        cwn::Log::error(QStringLiteral("Error intercepting URL %1: %2")
                            .arg(url.toString(), QString::fromLatin1(e.what())));
        return url;
    }

    return url;
}
