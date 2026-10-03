#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

// 显示与小组件增强扩展服务（four-plugins-to-extensions-plan §4.1，P1 More_settings 去补丁化重实现）。
// 对应上游 `com.kryon.more_settings` v1.3.4（约 3428 行：main/integrations/installer_backend/
// payload_host/config/qml/settings/tools），Next 落点为单扩展 `classwidgets.ext.displayTweaks` 收敛 4 组功能。
// 去补丁化核心：禁止 `integrations.py` 字符串锚点 + `.cwplugin_backups` + 整文件替换范式；
// 远端 payload 安装器（installer_backend：远端 manifest + PEP440 + sha256 + zip-slip + 事务验收）
// 舍弃为静态编译随包；`FEATURE_PATCH_SPECS` 转为启动健康自检（见 healthCheck）。
//
// 职责：
//   - QML 经上下文名 "DisplayTweaks" 读取开关与健康态（无状态门面，不持有
//     ConfigStore 指针；质检修正注释——原注释与构造实现不符）；具体行为
//     （动画/几何/不隐藏）由 QML 侧绑定 `Configs.data.extensions.display_tweaks.*`
//     直接驱动，本服务只做健康自检 + 排除科目解析，避免 QML 重复解析 JSON。
//   - 线程模型：全部 GUI 线程同步，无网络/定时器。
class DisplayTweaksService : public QObject
{
    Q_OBJECT
    // 健康态：补丁检测语义转健康自检——启动时校验关键 QML 挂载点是否存在、
// 配置键形状是否合法；失败只打 Logger + 设置页黄条，不要求卸载。
    Q_PROPERTY(bool healthy READ healthy NOTIFY healthChanged)
    Q_PROPERTY(QString healthMessage READ healthMessage NOTIFY healthChanged)

public:
    explicit DisplayTweaksService(QObject *parent = nullptr);

    bool healthy() const { return m_healthy; }
    QString healthMessage() const { return m_healthMessage; }

    // QML 契约：解析 `extensions.display_tweaks.hide_excluded_subjects`（JSON 数组字符串，
    // 兼容旧版逗号分隔 `hide_excluded_lessons` 语义），返回去重后的科目名列表（≤20 项截断）。
    Q_INVOKABLE QStringList excludedSubjects(const QString &jsonArray) const;
    // 启动健康自检：由 WidgetsWindow::onQmlReady 在主窗口 QML 就绪后以真实
    // findChild 结果调用（widgetsFlow/schedulePeekBar 挂载点存在性），
    // 本方法合并配置形状检查后更新 healthy/healthMessage。失败只记日志，不抛异常。
    Q_INVOKABLE bool healthCheck(bool widgetsFlowPresent, bool schedulePeekBarPresent);

signals:
    void healthChanged();
private:
    bool m_healthy = true;
    QString m_healthMessage;
};
