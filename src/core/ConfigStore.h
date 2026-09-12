#pragma once

#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QTimer>

#include <optional>

// 对应上游 src/core/config/manager.py 的 ConfigManager（QML 名 "Configs"）。
//
// 上游用 Pydantic 模型（config/model.py）声明全部字段与默认值；本移植按 M2 方案
// 保留 JSON 存储 + 手写默认值（省 4.9MB 的 pydantic_core，见移植方案 §0.5.5）。
// JSON 结构与 Pydantic model_dump_json 完全对齐，存量用户 configs.json 无感迁移。
// 默认树选择硬编码而非读 app/configs/：上游仓库同样不带默认 configs.json
// （默认值只存在于 Pydantic 模型），C++ 仓库的 app/configs/ 目前仅有 .gitkeep。
//
// 职责对照（均以 manager.py 行号为准）：
//   load_config()  → load()：读文件 + 默认值深合并（用户值优先、缺键补默认）
//                    + 迁移清理 + 版本/字体兜底（_ensure_defaults）
//                    + 叶子类型校验/纠正（对应 Pydantic 校验，轻量手写实现）
//                    + 清理过期 reschedule_day（_clean_useless_configs）+ 无条件 save()
//   save()         → save()：UTF-8、缩进 4（model_dump_json(indent=4)）
//   data 属性      → data()：整树转 QVariant（manager.py:160-162）
//   set()          → set()：点分路径写入；锁定拦截 + 值相等短路 + 叶子类型校验
//   lock/unlock    → lock()/unlock()：仅内存态（manager.py:52 的 locked_keys 不落盘），
//                    上游唯一写入方是插件 API（plugin/components.py:596-612）
//   isKeyLocked()  → isKeyLocked()：精确全路径匹配——上游 model.py:69 只比对完整
//                    键名，锁 "preferences" 不会拦截 "preferences.font"
//
// QML 契约（137 个零改动 QML 的使用面，grep 实测 35 文件 / data 126 + isKeyLocked 72
// + set 69 处）：
//   Configs.data.<点分路径>            → Q_PROPERTY data（QVariantMap 嵌套）
//   Configs.set(key, value)            → 点分路径写入
//   Configs.isKeyLocked(key)           → 锁定检查
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
    // 写回磁盘（对应 save()，UTF-8、缩进 4）
    void save(bool silent = false);

    QVariant data() const;

    Q_INVOKABLE bool isKeyLocked(const QString &key) const;
    Q_INVOKABLE void set(const QString &key, const QVariant &value);

    // 对应 lock()/unlock()（manager.py:140-152，接受 str | list | set）
    Q_INVOKABLE void lock(const QStringList &keys);
    Q_INVOKABLE void unlock(const QStringList &keys);
    // 单键便捷重载（上游接受裸字符串；QML 不调用，供 C++ 插件 API 层备用）
    Q_INVOKABLE void lock(const QString &key);
    Q_INVOKABLE void unlock(const QString &key);
    // 对应 GlobalConfigAPI.locked_keys 属性（plugin/components.py:613-615）
    QStringList lockedKeys() const;

    // --- C++ 内部读取辅助 ---
    // 取点分路径的值；不存在时返回 std::nullopt（schedule/ 域在用）
    std::optional<QJsonValue> value(const QString &dottedKey) const;
    // 内部写入（不检查锁定，用于模型回写）；行为同 set()
    void setInternal(const QString &dottedKey, const QJsonValue &value);

    // 启动定时落盘（对应 manager.py:48-50 的 1 分钟保存定时器）
    void startAutoSave();

signals:
    void dataChanged();

private:
    static QJsonObject defaultConfig();
    static QJsonObject mergeDefaults(const QJsonObject &defaults, const QJsonObject &loaded);
    void sanitize();
    void ensureDefaults();
    void cleanUselessConfigs();
    void removeLegacyPlazaSources();
    QString filePath() const;

    QString m_configsDir;
    QJsonObject m_json;
    QSet<QString> m_lockedKeys;
    QTimer m_saveTimer;
    bool m_dirty = false;
};
