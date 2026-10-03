#include "WidgetsModel.h"

#include "ConfigStore.h"
#include "Logger.h"
#include "extensions/ExtensionManager.h"
#include "weather/WeatherService.h"

#include <QUuid>

namespace {
QString newInstanceId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QVariantMap definitionToVariantMap(const WidgetDefinition &def)
{
    // 键名对齐 model.py add_widget 的 definition 字典（AddWidgetsDialog 读取）
    QVariantMap map;
    map.insert(QStringLiteral("id"), def.id);
    map.insert(QStringLiteral("name"), def.name);
    map.insert(QStringLiteral("qml_path"), def.qmlPath.toString());
    map.insert(QStringLiteral("backend_obj"), QVariant::fromValue(def.backendObj));
    map.insert(QStringLiteral("settings_qml"), def.settingsQml.toString());
    map.insert(QStringLiteral("default_settings"), def.defaultSettings);
    map.insert(QStringLiteral("max_instances"), def.maxInstances);
    return map;
}
} // namespace

WidgetsModel::WidgetsModel(QObject *parent)
    : QAbstractListModel(parent)
{
    connect(this, &WidgetsModel::modelChanged, this, &WidgetsModel::saveConfig);
}

QHash<int, QByteArray> WidgetsModel::roleNames() const
{
    // model.py:57-68 role 名逐字对齐（+ overlayMember：four-plugins B 新增，不改既有 9 个）
    return {
        { InstanceIdRole, "instanceId" },
        { TypeIdRole, "typeId" },
        { NameRole, "name" },
        { IconRole, "icon" },
        { QmlPathRole, "qmlPath" },
        { BackendRole, "backendObj" },
        { SettingsRole, "settings" },
        { SettingsQmlRole, "settingsQml" },
        { WidgetIdRole, "widget_id" },
        { OverlayMemberRole, "overlayMember" },
    };
}

int WidgetsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_instances.size();
}

QVariant WidgetsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_instances.size())
        return {};

    const WidgetInstance &w = m_instances.at(index.row());
    switch (role) {
    case InstanceIdRole:
        return w.instanceId;
    case TypeIdRole:
        return w.typeId;
    case NameRole: {
        const auto it = m_definitions.constFind(w.typeId);
        return it != m_definitions.cend() ? it->name : QString();
    }
    case IconRole:
        return QString();
    case QmlPathRole: {
        const auto it = m_definitions.constFind(w.typeId);
        return it != m_definitions.cend() ? it->qmlPath.toString() : QString();
    }
    case BackendRole: {
        const auto it = m_definitions.constFind(w.typeId);
        return it != m_definitions.cend() ? QVariant::fromValue(it->backendObj) : QVariant();
    }
    case SettingsRole:
        return w.settings;
    case SettingsQmlRole: {
        const auto it = m_definitions.constFind(w.typeId);
        return it != m_definitions.cend() ? it->settingsQml.toString() : QString();
    }
    case WidgetIdRole:
        // model.py: w.get("id", w.get("type_id")) —— definition.id 恒等于 type_id
        return w.typeId;
    case OverlayMemberRole:
        // overlay 堆叠成员标记：settings._overlayMember 真值（presets 摆放同样经该键持久化）
        return w.settings.value(QStringLiteral("_overlayMember")).toBool();
    default:
        return {};
    }
}

QVector<WidgetsModel::PresetEntry> WidgetsModel::normalizeEntries(const QVariantList &entries) const
{
    // model.py _normalize_preset_entries：条目可为字符串（仅 type_id）
    // 或 {type_id, instance_id?, settings?} 字典
    QVector<PresetEntry> normalized;
    for (const QVariant &e : entries) {
        if (e.metaType().id() == QMetaType::QString) {
            PresetEntry entry;
            entry.typeId = e.toString();
            entry.instanceId = newInstanceId();
            normalized.append(entry);
            continue;
        }
        if (!e.canConvert<QVariantMap>())
            continue;
        const QVariantMap map = e.toMap();
        if (!map.contains(QStringLiteral("type_id")))
            continue;
        PresetEntry entry;
        entry.typeId = map.value(QStringLiteral("type_id")).toString();
        entry.instanceId = map.value(QStringLiteral("instance_id")).toString();
        if (entry.instanceId.isEmpty())
            entry.instanceId = newInstanceId();
        entry.settings = map.value(QStringLiteral("settings")).toMap();
        normalized.append(entry);
    }
    return normalized;
}

void WidgetsModel::loadConfig()
{
    if (!m_configStore) {
        cwn::Log::warn(QStringLiteral("Cannot load widget presets: ConfigStore not available"));
        return;
    }

    const QVariant data = m_configStore->data();
    const QVariantMap preferences =
        data.toMap().value(QStringLiteral("preferences")).toMap();

    m_presets.clear();
    const QVariantMap rawPresets =
        preferences.value(QStringLiteral("widgets_presets")).toMap();
    for (auto it = rawPresets.constBegin(); it != rawPresets.constEnd(); ++it)
        m_presets.insert(it.key(), normalizeEntries(it.value().toList()));

    const QString current =
        preferences.value(QStringLiteral("current_preset")).toString();
    if (!current.isEmpty())
        loadPreset(current);
}

void WidgetsModel::saveConfig()
{
    if (!m_configStore)
        return;
    // save_config：写回 presets + current_preset（ConfigStore 落盘由自动保存接管）
    m_configStore->set(QStringLiteral("preferences.widgets_presets"), presets());
    m_configStore->set(QStringLiteral("preferences.current_preset"), m_currentPreset);
    cwn::Log::info(QStringLiteral("Widget presets saved"));
}

void WidgetsModel::syncCurrentPreset()
{
    if (m_currentPreset.isEmpty())
        return;
    QVector<PresetEntry> entries;
    for (const WidgetInstance &w : m_instances) {
        PresetEntry entry;
        entry.typeId = w.typeId;
        entry.instanceId = w.instanceId;
        entry.settings = w.settings;
        entries.append(entry);
    }
    m_presets.insert(m_currentPreset, entries);
}

QVariantMap WidgetsModel::presets() const
{
    QVariantMap result;
    for (auto it = m_presets.constBegin(); it != m_presets.constEnd(); ++it) {
        QVariantList list;
        for (const PresetEntry &entry : it.value()) {
            QVariantMap entryMap;
            entryMap.insert(QStringLiteral("type_id"), entry.typeId);
            entryMap.insert(QStringLiteral("instance_id"), entry.instanceId);
            entryMap.insert(QStringLiteral("settings"), entry.settings);
            list.append(entryMap);
        }
        result.insert(it.key(), list);
    }
    return result;
}

QVariantList WidgetsModel::definitionsList() const
{
    QVariantList list;
    for (const WidgetDefinition &def : m_definitions) {
        // §5 B1：天气小组件与扩展 classwidgets.ext.weather 联动——未启用时不进
        // 「添加小组件」列表。过滤条件只命中天气的 type_id，其余内置定义
        // （含将来注册的插件组件）不受影响；扩展框架未注入时同样全量放行。
        // 开关翻转后由 AppCentral 接线调 refreshDefinitions() 触发 QML 重取。
        if (m_extensionManager && def.id == WeatherService::widgetTypeId()
            && !m_extensionManager->isEnabled(QStringLiteral("classwidgets.ext.weather")))
            continue;
        list.append(definitionToVariantMap(def));
    }
    return list;
}

int WidgetsModel::instanceCount(const QString &typeId) const
{
    int count = 0;
    for (const WidgetInstance &w : m_instances) {
        if (w.typeId == typeId)
            ++count;
    }
    return count;
}

void WidgetsModel::updatePreset(const QString &presetName, const QVariantList &entries)
{
    m_presets.insert(presetName, normalizeEntries(entries));
    if (m_currentPreset == presetName)
        loadPreset(presetName);
    emit modelChanged();
}

void WidgetsModel::setPreset(const QString &presetName, const QVariantList &entries)
{
    m_presets.insert(presetName, normalizeEntries(entries));
}

void WidgetsModel::loadPreset(const QString &presetName)
{
    if (!m_presets.contains(presetName))
        return;

    const QVector<PresetEntry> entries = m_presets.value(presetName);
    QVector<WidgetInstance> newInstances;
    for (const PresetEntry &entry : entries) {
        if (!m_definitions.contains(entry.typeId))
            continue;
        const WidgetDefinition &definition = m_definitions.value(entry.typeId);

        WidgetInstance instance;
        instance.instanceId = entry.instanceId.isEmpty() ? newInstanceId() : entry.instanceId;
        instance.typeId = entry.typeId;
        instance.settings = definition.defaultSettings;
        for (auto it = entry.settings.constBegin(); it != entry.settings.constEnd(); ++it)
            instance.settings.insert(it.key(), it.value());
        newInstances.append(instance);
    }

    beginResetModel();
    m_instances = newInstances;
    m_currentPreset = presetName;
    endResetModel();
    ++m_instancesRevision;
    emit modelChanged();
}

void WidgetsModel::addWidget(const WidgetDefinition &definition)
{
    if (definition.id.isEmpty()) {
        cwn::Log::warn(
            QStringLiteral("Cannot register widget: Invalid type_id \"%1\"").arg(definition.id));
        return;
    }

    // 已存在时仅更新名称（翻译刷新），与 model.py add_widget 一致
    if (m_definitions.contains(definition.id)) {
        m_definitions[definition.id].name = definition.name;
        cwn::Log::debug(QStringLiteral("Updated widget name for %1: %2")
                            .arg(definition.id, definition.name));
    } else {
        m_definitions.insert(definition.id, definition);
    }
    emit definitionChanged();
}

void WidgetsModel::addInstance(const QString &typeId)
{
    if (!m_definitions.contains(typeId))
        return;
    const WidgetDefinition &definition = m_definitions.value(typeId);

    // 同一预设内实例数上限（0 = 不限量）；仅拦截新增，不破坏手改配置塞进的存量
    if (definition.maxInstances > 0 && instanceCount(typeId) >= definition.maxInstances) {
        cwn::Log::warn(QStringLiteral("Cannot add instance of \"%1\": limit %2 reached")
                           .arg(typeId)
                           .arg(definition.maxInstances));
        return;
    }

    WidgetInstance instance;
    instance.instanceId = newInstanceId();
    instance.typeId = typeId;
    instance.settings = definition.defaultSettings;

    beginInsertRows(QModelIndex(), m_instances.size(), m_instances.size());
    m_instances.append(instance);
    endInsertRows();
    syncCurrentPreset();
    ++m_instancesRevision;
    emit modelChanged();
}

void WidgetsModel::moveInstance(int fromIndex, int toIndex)
{
    if (fromIndex == toIndex || fromIndex < 0 || toIndex < 0
        || fromIndex >= m_instances.size() || toIndex >= m_instances.size())
        return;
    // beginMoveRows 的目标行语义与 Python 端保持一致
    beginMoveRows(QModelIndex(), fromIndex, fromIndex, QModelIndex(),
                  toIndex < fromIndex ? toIndex : toIndex + 1);
    m_instances.move(fromIndex, toIndex);
    endMoveRows();
    syncCurrentPreset();
    emit modelChanged();
}

void WidgetsModel::removeInstance(const QString &instanceId)
{
    for (int i = 0; i < m_instances.size(); ++i) {
        if (m_instances.at(i).instanceId == instanceId) {
            // four-plugins B 卸载保护：overlay 锁定成员（settings._overlayLocked）拒绝删除，
            // 由调用方（右键菜单/就地编辑行）提示；避免堆叠态误删导致布局断裂。
            if (m_instances.at(i).settings.value(QStringLiteral("_overlayLocked")).toBool()) {
                cwn::Log::warn(QStringLiteral("WidgetsModel: refuse to remove overlay-locked %1")
                                   .arg(instanceId));
                return;
            }
            beginRemoveRows(QModelIndex(), i, i);
            m_instances.removeAt(i);
            endRemoveRows();
            syncCurrentPreset();
            ++m_instancesRevision;
            emit modelChanged();
            return;
        }
    }
}

void WidgetsModel::removeAllInstancesOf(const QString &typeId)
{
    // §5 B2（扩展整体下线）：removeInstance 按 instanceId 只作用于当前预设视图
    // （m_instances），关闭开关要求「全部预设都不再保留该类型实例」。实例的
    // 唯一事实源是 m_presets（saveConfig 全量写回 preferences.widgets_presets），
    // 因此先遍历所有预设删除条目，再逐行移除当前视图（begin/endRemoveRows 让
    // QML 侧正确销毁组件），最后走既有 syncCurrentPreset + modelChanged→saveConfig
    // 持久化通路，不绕过模型直写配置。
    bool removedAny = false;
    for (auto it = m_presets.begin(); it != m_presets.end(); ++it) {
        QVector<PresetEntry> &entries = it.value();
        for (int i = entries.size() - 1; i >= 0; --i) {
            if (entries.at(i).typeId == typeId) {
                entries.remove(i);
                removedAny = true;
            }
        }
    }
    // 倒序删除，行号不会因先前移除而失效
    for (int i = m_instances.size() - 1; i >= 0; --i) {
        if (m_instances.at(i).typeId != typeId)
            continue;
        beginRemoveRows(QModelIndex(), i, i);
        m_instances.remove(i);
        endRemoveRows();
        removedAny = true;
    }
    if (!removedAny)
        return; // 无任何匹配实例：不发通知、不写配置（避免空操作落盘）
    syncCurrentPreset();
    ++m_instancesRevision;
    emit modelChanged();
}

void WidgetsModel::updateSettings(const QString &instanceId, const QVariantMap &settings)
{
    for (int i = 0; i < m_instances.size(); ++i) {
        if (m_instances.at(i).instanceId == instanceId) {
            QVariantMap merged = m_instances.at(i).settings;
            for (auto it = settings.constBegin(); it != settings.constEnd(); ++it)
                merged.insert(it.key(), it.value());
            m_instances[i].settings = merged;

            const QModelIndex ix = index(i);
            Q_EMIT dataChanged(ix, ix, { SettingsRole, OverlayMemberRole });
            syncCurrentPreset();
            emit modelChanged();
            return;
        }
    }
}

void WidgetsModel::setOverlayEditingId(const QString &instanceId)
{
    if (m_overlayEditingId == instanceId)
        return;
    m_overlayEditingId = instanceId;
    emit overlayStateChanged();
}

void WidgetsModel::setOverlayListMode(bool enabled)
{
    if (m_overlayListMode == enabled)
        return;
    m_overlayListMode = enabled;
    emit overlayStateChanged();
}
