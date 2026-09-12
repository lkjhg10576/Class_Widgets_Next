#pragma once

#include <QQmlAbstractUrlInterceptor>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QUrl>

// 对应上游 src/core/themes/interceptor.py 的 ThemeUrlInterceptor。
// 语义：QML 引擎请求的文件 URL 中若含有 "ClassWidgets/theme" 片段，则优先路由到
// 当前主题目录下的同名文件（主题覆盖层），不存在时回退默认主题（src/qml）下的文件；
// 命中改写时附加 t=<nonce> 查询参数击穿 QML 类型缓存（interceptor.py:90-98）。
//
// 注意（Qt6 移植要点）：
//   - QQmlAbstractUrlInterceptor 在 Qt6 中是纯接口（非 QObject、无 Q_OBJECT），
//     因此本类以 QObject 为第一基类提供信号/所有权，接口为第二基类；
//   - intercept() 会被 QML 引擎在多个线程调用（Qt 文档要求线程安全），用 QMutex 保护；
//   - 上游 core.py:34 经 engine.setUrlInterceptor 安装；Qt6 对应
//     QQmlEngine::addUrlInterceptor（QQuickUrlInterceptor 已废弃）。
class ThemeUrlInterceptor : public QObject, public QQmlAbstractUrlInterceptor
{
    Q_OBJECT
public:
    explicit ThemeUrlInterceptor(QObject *parent = nullptr);

    // interceptor.py:18-33 set_theme：空串解除拦截；非法路径告警并解除；
    // 生效时重新生成 nonce（保证新旧主题的 URL 缓存互不污染）
    void setThemePath(const QString &themePath);

    // interceptor.py:34-103 intercept
    QUrl intercept(const QUrl &url, DataType type) override;

private:
    QString makeNonce() const;
    QUrl makeNonceUrl(const QString &localFile) const;

    QMutex m_mutex;
    QString m_currentThemePath; // 已统一 '/' 分隔；空 = 未启用拦截
    QString m_nonce;
    QString m_targetPath = QStringLiteral("ClassWidgets/theme"); // interceptor.py:16
};
