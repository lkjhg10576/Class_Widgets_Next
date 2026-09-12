#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

class NotificationService;

// 对应上游 src/core/notification/provider.py —— 一个 Provider = 一个通知来源
// （模块 / 插件）。上游 provider 构造时自动向 NotificationManager 注册
// （provider.py:45-46），C++ 侧保持同样语义：service 为空时回退到
// NotificationService::defaultInstance()（对应 provider.py:39-41 从
// AppCentral.instance() 自动取 manager 的行为）。
class NotificationProvider : public QObject
{
    Q_OBJECT
    // manager.py:31-37：注册时要求 id/name 属性存在；id 注册后不可变（CONSTANT）
    Q_PROPERTY(QString id READ id CONSTANT)
    // name 支持运行期改名（上游 retranslate 时重建 provider，runtime.py:118-135；
    // C++ 侧用 setName 原地改名 + notificationProvidersChanged 刷新列表）
    Q_PROPERTY(QString name READ name NOTIFY nameChanged)
    // provider.py:31-36 图标：字体图标名或图片 URI（Path 已在上游转 as_uri()，
    // 调用方传入前自行用 AppPaths::uriOf 转换）
    Q_PROPERTY(QString icon READ icon CONSTANT)
    // provider.py:23 use_system_notify：Provider 是否"支持"系统通知（能力标记，
    // 与配置里的 use_system_notify 开关是两回事，见 manager.py:113）
    Q_PROPERTY(bool useSystemNotify READ useSystemNotify CONSTANT)

public:
    // provider.py:18-26 __init__(id, name, icon, use_system_notify, manager)
    explicit NotificationProvider(const QString &id, const QString &name,
                                  const QString &icon = QString(),
                                  bool useSystemNotify = false,
                                  NotificationService *service = nullptr,
                                  QObject *parent = nullptr);

    QString id() const { return m_id; }
    QString name() const { return m_name; }
    QString icon() const { return m_icon; }
    bool useSystemNotify() const { return m_useSystemNotify; }
    void setName(const QString &name); // 供 retranslate 更新显示名（NOTIFY nameChanged）

    // provider.py:49-56 get_config：读取该 provider 的配置段
    // notifications.providers.<id>（缺省即 NotificationProviderConfig() 默认值）。
    // 返回键与 model.py:40-43 一致：enabled / use_system_notify / use_app_notify
    QVariantMap config() const;

    // provider.py:58-81 push —— QML 调用签名（provider.py:58）:
    //   push(level, title, message, duration, closable)
    // Debugger/contents/Overview.qml:46-52 即以 5 参形式调用。
    // cfg.enabled 为 false 时直接忽略（provider.py:67-69）。
    Q_INVOKABLE void push(int level, const QString &title, const QString &message,
                          int duration, bool closable);

signals:
    void nameChanged(const QString &name);

private:
    QString m_id;
    QString m_name;
    QString m_icon;
    bool m_useSystemNotify = false;
    NotificationService *m_service = nullptr; // 构造时注册（provider.py:45-46）
};
