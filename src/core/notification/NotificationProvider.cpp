#include "NotificationProvider.h"

#include "NotificationService.h"

#include "../Logger.h"

#include <QVariantMap>

using cwn::notification::NotificationData;

NotificationProvider::NotificationProvider(const QString &id, const QString &name,
                                           const QString &icon, bool useSystemNotify,
                                           NotificationService *service, QObject *parent)
    : QObject(parent)
    , m_id(id)
    , m_name(name)
    , m_icon(icon)
    , m_useSystemNotify(useSystemNotify)
{
    // provider.py:39-43：未显式传入 manager 时自动从全局取（上游取
    // AppCentral.instance().notification；C++ 侧等价物是 defaultInstance()）
    m_service = service ? service : NotificationService::defaultInstance();

    // provider.py:45-46：自动注册
    if (m_service) {
        m_service->registerProvider(this);
    } else {
        cwn::Log::warn(QStringLiteral("NotificationProvider '%1' created without a "
                                      "NotificationService; push() will be ignored.")
                           .arg(id));
    }
}

void NotificationProvider::setName(const QString &name)
{
    if (m_name == name)
        return;
    m_name = name;
    emit nameChanged(m_name);
}

QVariantMap NotificationProvider::config() const
{
    // provider.py:49-56 get_config：读取 notifications.providers.<id>，
    // 缺省返回 NotificationProviderConfig() 的默认值（model.py:40-43）。
    return m_service ? m_service->providerConfig(m_id)
                     : QVariantMap{ { QStringLiteral("enabled"), true },
                                    { QStringLiteral("use_system_notify"), false },
                                    { QStringLiteral("use_app_notify"), true } };
}

void NotificationProvider::push(int level, const QString &title, const QString &message,
                                int duration, bool closable)
{
    // provider.py:67-69：provider 被禁用时忽略
    const QVariantMap cfg = config();
    if (!cfg.value(QStringLiteral("enabled"), true).toBool())
        return;

    // provider.py:71-79 NotificationData(...)：icon 取 Provider 图标
    NotificationData data;
    data.providerId = m_id;
    data.level = level;
    data.title = title;
    data.message = message;
    data.duration = duration;
    data.closable = closable;
    data.icon = m_icon;

    // provider.py:81 manager.dispatch(data, cfg)
    if (m_service)
        m_service->dispatch(data);
}
