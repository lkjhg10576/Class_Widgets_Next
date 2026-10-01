#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>

class ConfigStore;

// 「扩展功能」框架（见 extensions-feature-plan.md §4 阶段 A）。
// 维护扩展静态注册表（id/名称/图标/描述/设置页）与启用集合
// （配置键 extensions.enabled），以 QML 上下文名 "Extensions" 暴露。
//
// 与插件系统严格分离：插件是社区代码（plugins.* 键 / PluginManagerStub，
// Phase 2 空壳）；扩展是官方功能模块，仅通过设置界面开关与调参。
// 天气/随机点名/课表速览三项的具体行为由后续阶段（B/C/D）接线实现。
class ExtensionManager : public QObject
{
    Q_OBJECT
    // 列表页数据源：每项 {id, name, icon, description, enabled, hasSettings, settingsPage}
    Q_PROPERTY(QVariantList extensions READ extensions NOTIFY extensionsChanged)

public:
    // 单个扩展定义。settingsPageQml 可空（无设置页的扩展）；非空时为
    // PathManager.qml() 可解析的相对路径（pages/settings/Extensions/*.qml）
    struct ExtensionDefinition
    {
        QString id;
        QString name;
        QString icon;
        QString description;
        QString settingsPageQml;
    };

    // configs 必须已完成 load()（构造函数内执行 §5 B3 的存量天气迁移，需读到
    // 磁盘上的 preferences.widgets_presets 原值）；AppCentral::initialize 的
    // 创建时机已满足该前置
    explicit ExtensionManager(ConfigStore *configs, QObject *parent = nullptr);

    QVariantList extensions() const;

    // --- QML 契约 ---
    Q_INVOKABLE bool isEnabled(const QString &id) const;
    // 写回 extensions.enabled 并即时落盘；状态实际无变化（含键被锁定
    // 导致写入被拒）时不发 extensionToggled
    Q_INVOKABLE void setEnabled(const QString &id, bool enabled);

    // C++ 侧遍历注册表（后续阶段 B/C/D 的接线点）；每次调用现取，
    // 使 translate() 名称随语言切换即时生效
    static QList<ExtensionDefinition> definitions();

signals:
    void extensionsChanged();
    void extensionToggled(const QString &id, bool enabled);

private:
    // 读取 extensions.enabled；缺失/非数组/非字符串元素按忽略处理
    // （结构校验已由 ConfigStore::sanitize 在 load 时完成，这里只做防御）
    QStringList enabledIds() const;
    static bool hasDefinition(const QString &id);

    // 阶段 B 存量天气配置一次性迁移（extensions-feature-plan §5 B3），构造时
    // 执行、幂等：扫描 preferences.widgets_presets，任一预设含天气小组件实例
    // → 自动启用 classwidgets.ext.weather（老用户天气不丢失），并把首个非空
    // 实例 settings.city 收敛为全局 weather.city。启用写入不触发 extensionToggled：
    // 此刻 AppCentral 尚未接线（connectServices 在其后才执行），且启动期实例
    // 本就由 WidgetsModel::loadConfig 从配置装载，无需信号驱动的补加/补删。
    void migrateLegacyWeatherConfig();

    ConfigStore *m_configs;
};
