#pragma once

#include <QString>
#include <QVariantMap>

// 对应上游 src/core/notification/model.py —— 通知数据形状（纯数据，无 QObject）。
// QML 消费面（FloatingWidget.qml:57-61 / dynamicNotification.qml:194-206）只读取
// payload 的 title/message/icon/level/duration 五个键，但 payload 形状必须与
// model.py:29-38 的 NotificationPayload TypedDict 逐键一致（含 provider_id、
// closable、silent、use_system），保证未来插件/QML 无缝兼容。
namespace cwn {
namespace notification {

// model.py:7-11 NotificationLevel(IntEnum)
// 0=普通提示 1=上下课/状态 2=更新/风险 3=内部
enum NotificationLevel : int
{
    LevelInfo = 0,
    LevelAnnouncement = 1,
    LevelWarning = 2,
    LevelSystem = 3,
};

// model.py:14-25 NotificationData(BaseModel)
// message/icon 允许为空：QString()（null QVariant）对应上游 Optional[str] = None
struct NotificationData
{
    QString providerId;      // model.py:16 来源 Provider ID
    int level = 0;           // model.py:17 实际由前端映射样式
    QString title;           // model.py:18
    QString message;         // model.py:19（可空）
    QString icon;            // model.py:20 字体图标名或图片 URI

    // model.py:22-25 行为 & 展示
    int duration = 4000;     // ms，0 = 常驻
    bool closable = true;
    bool silent = false;     // 是否无声音
    bool useSystem = false;  // 系统通知 or 应用内（dispatch 时会被覆盖，见 manager.py:106）
};

// model.py:29-38 NotificationPayload（TypedDict）→ QVariantMap。
// 键名照上游蛇形命名（provider_id/use_system），QML 侧按 title/message/icon/
// level/duration 消费；其余键保持形状一致以便插件体系（Phase 2）复用。
inline QVariantMap toPayload(const NotificationData &data)
{
    return {
        { QStringLiteral("provider_id"), data.providerId },
        { QStringLiteral("level"), data.level },
        { QStringLiteral("title"), data.title },
        { QStringLiteral("message"), data.message },
        { QStringLiteral("icon"), data.icon },
        { QStringLiteral("duration"), data.duration },
        { QStringLiteral("closable"), data.closable },
        { QStringLiteral("silent"), data.silent },
        { QStringLiteral("use_system"), data.useSystem },
    };
}

// service.py:159-165 level_audio_mapping：级别 → 默认音效文件（assets/audio/ 下）
inline QString defaultLevelSound(int level)
{
    switch (level) {
    case LevelAnnouncement: return QStringLiteral("announcement.wav");
    case LevelWarning:      return QStringLiteral("warning.wav");
    case LevelSystem:       return QStringLiteral("system.wav");
    case LevelInfo:
    default:                return QStringLiteral("info.wav"); // service.py:165 get(level, "info.wav")
    }
}

} // namespace notification
} // namespace cwn
