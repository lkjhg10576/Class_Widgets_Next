#pragma once

#include <QAbstractListModel>
#include <QUrl>
#include <QVariantMap>
#include <QVector>

class ConfigStore;

// 小组件定义（对应 model.py 的 WidgetDefinition TypedDict）
struct WidgetDefinition
{
    QString id;          // type_id，同时是 widget_id
    QString name;
    QUrl qmlPath;
    QObject *backendObj = nullptr;
    QUrl settingsQml;
    QVariantMap defaultSettings;
};

struct WidgetInstance
{
    QString instanceId;
    QString typeId;
    QVariantMap settings;
};

// 对应上游 core/widgets/model.py 的 WidgetListModel。
//
// ⚠️ 契约纪律（移植方案 §0.5.8）：9 个 role 名与 slot 名一字不改 ——
// 这是将来插件系统把小组件注册回应用的唯一接口（WidgetLoader.qml 依赖）。
class WidgetsModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString currentPreset READ currentPreset NOTIFY modelChanged)
    Q_PROPERTY(QVariantMap presets READ presets NOTIFY modelChanged)
    Q_PROPERTY(QVariantList definitionsList READ definitionsList NOTIFY definitionChanged)

public:
    enum Roles {
        InstanceIdRole = Qt::UserRole + 1,
        TypeIdRole = Qt::UserRole + 2,
        NameRole = Qt::UserRole + 3,
        IconRole = Qt::UserRole + 4,
        QmlPathRole = Qt::UserRole + 5,
        BackendRole = Qt::UserRole + 6,
        SettingsRole = Qt::UserRole + 7,
        SettingsQmlRole = Qt::UserRole + 8,
        WidgetIdRole = Qt::UserRole + 9,
    };
    Q_ENUM(Roles)

    explicit WidgetsModel(QObject *parent = nullptr);

    // --- QAbstractListModel ---
    QHash<int, QByteArray> roleNames() const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;

    // --- QML 契约 slot（model.py:218-271）---
    Q_INVOKABLE void addInstance(const QString &typeId);
    Q_INVOKABLE void moveInstance(int fromIndex, int toIndex);
    Q_INVOKABLE void removeInstance(const QString &instanceId);
    Q_INVOKABLE void updateSettings(const QString &instanceId, const QVariantMap &settings);
    Q_INVOKABLE void updatePreset(const QString &presetName, const QVariantList &entries);
    Q_INVOKABLE void setPreset(const QString &presetName, const QVariantList &entries);
    Q_INVOKABLE void loadPreset(const QString &presetName);

    // --- C++ 注册入口（model.py add_widget；将来 IWidgetProvider 经此挂载）---
    void addWidget(const WidgetDefinition &definition);

    // 从 ConfigStore 读取 widgets_presets + current_preset（model.py load_config）
    void loadConfig();
    void setConfigStore(ConfigStore *store) { m_configStore = store; }

    QString currentPreset() const { return m_currentPreset; }
    QVariantMap presets() const;
    QVariantList definitionsList() const;

signals:
    void modelChanged();
    void definitionChanged();

private slots:
    // model.py:55 modelChanged.connect(save_config)
    void saveConfig();

private:
    struct PresetEntry
    {
        QString typeId;
        QString instanceId;
        QVariantMap settings;
    };
    // model.py _normalize_preset_entries：接受字符串或 {type_id, instance_id?, settings?} 字典
    QVector<PresetEntry> normalizeEntries(const QVariantList &entries) const;
    void syncCurrentPreset();

    QHash<QString, WidgetDefinition> m_definitions;   // key: type_id
    QVector<WidgetInstance> m_instances;
    QHash<QString, QVector<PresetEntry>> m_presets;
    QString m_currentPreset;
    ConfigStore *m_configStore = nullptr;
};
