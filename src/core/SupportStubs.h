#pragma once

#include <QObject>

// 尚未落地（Phase 2 插件系统）的占位对象。
// M2-M4 已用真实实现替换了此前的 Translator/Notification/ScheduleRuntime/
// ScheduleManager/ScheduleEditor/WindowManager/ClassSwapManager/UtilsBackend
// 八个 stub（分别位于 utils/、notification/、schedule/、windows/）。

// 对应 core/plugin/manager.py PluginManager —— Phase 2；
// 插件专属 QML 页面不参与构建（见移植方案 §0.5.8），仅注册名字防绑定缺失
class PluginManagerStub : public QObject
{
    Q_OBJECT
public:
    explicit PluginManagerStub(QObject *parent = nullptr);
};
