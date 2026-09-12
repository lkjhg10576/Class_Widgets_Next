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

QUrl ThemeUrlInterceptor::makeNonceUrl(const QString &localFile) const
{
    QUrl result = QUrl::fromLocalFile(localFile);
    result.setQuery(QStringLiteral("t=") + m_nonce);
    return result;
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

    // interceptor.py:71-82：防循环重定向——源路径已位于当前主题目录下
    if (lowerSource.startsWith(m_currentThemePath.toLower())) {
        if (QFileInfo::exists(source))
            return url;
        const QString defaultFile =
            AppPaths::instance().qmlRoot() + QLatin1Char('/') + relativePart;
        if (QFileInfo::exists(defaultFile))
            return makeNonceUrl(defaultFile);
        return url;
    }

    try {
        // interceptor.py:84-98：先找主题内文件，其次回退默认主题（src/qml）
        const QString targetFile = m_currentThemePath + QLatin1Char('/') + relativePart;
        if (QFileInfo::exists(targetFile))
            return makeNonceUrl(targetFile);

        const QString defaultFile =
            AppPaths::instance().qmlRoot() + QLatin1Char('/') + relativePart;
        if (QFileInfo::exists(defaultFile))
            return makeNonceUrl(defaultFile);
    } catch (const std::exception &e) {
        // interceptor.py:100-101：拦截异常只记录，不中断加载
        cwn::Log::error(QStringLiteral("Error intercepting URL %1: %2")
                            .arg(url.toString(), QString::fromLatin1(e.what())));
        return url;
    }

    return url;
}
