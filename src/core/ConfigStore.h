#pragma once

#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QTimer>

#include <optional>

// 对应上游 src/core/config/manager.py 的 ConfigManager（QML 名 "Configs"）。
//
// 上游用 Pydantic 模型；本移植按 M2 方案保留 JSON 存储 + 手写默认值
// （省 4.9MB 的 pydantic_core，见移植方案 §0.5.5）。
// JSON 结构与 Pydantic model_dump_json 完全对齐，存量用户 configs.json 无感迁移。
//
// QML 契约（M1 使用面，见 CW2-M1骨架任务拆分.md §1.6）：
//   Configs.data.preferences.xxx          → Q_PROPERTY data（QVariantMap 嵌套）
//   Configs.set(key, value)               → 点分路径写入
//   Configs.isKeyLocked(key)              → 锁定检查
//   Connections { target: Configs; function onDataChanged() }
//     —— handler 名来自 data 属性的 NOTIFY 信号，故信号名必须为 dataChanged
class ConfigStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariant data READ data NOTIFY dataChanged)

public:
    explicit ConfigStore(QString configsDir, QObject *parent = nullptr);

    // 读取 configs/configs.json；缺失或损坏时用内置默认值（不崩，对应 load_config）
    void load();
    // 写回磁盘（对应 save()，缩进 4 空格）
    void save(bool silent = false);

    QVariant data() const;

    Q_INVOKABLE bool isKeyLocked(const QString &key) const;
    Q_INVOKABLE void set(const QString &key, const QVariant &value);

    // 对应 lock()/unlock()（参数为键名列表）
    Q_INVOKABLE void lock(const QStringList &keys);
    Q_INVOKABLE void unlock(const QStringList &keys);

    // --- C++ 内部读取辅助 ---
    // 取点分路径的值；不存在时返回 std::nullopt
    std::optional<QJsonValue> value(const QString &dottedKey) const;
    // 内部写入（不检查锁定，用于模型回写）；行为同 set()
    void setInternal(const QString &dottedKey, const QJsonValue &value);

    // 启动定时落盘（对应 1 分钟保存定时器）
    void startAutoSave();

signals:
    void dataChanged();

private:
    static QJsonObject defaultConfig();
    static QJsonObject mergeDefaults(const QJsonObject &defaults, const QJsonObject &loaded);
    void ensureDefaults();
    QString filePath() const;

    QString m_configsDir;
    QJsonObject m_json;
    QSet<QString> m_lockedKeys;
    QTimer m_saveTimer;
    bool m_dirty = false;
};
