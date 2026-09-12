#pragma once

#include <QObject>
#include <QString>

class ScheduleManager;

// 对应上游 core/convertor/slots.py（115 行）的 QML 后端桥。上游该类名为
// ScheduleIO，挂在 ScheduleManager 上（manager.py:41 构造，53-55 经
// @Property scheduleIO 暴露），QML 仅在编辑器页调用：
//   AppCentral.scheduleManager.scheduleIO.exportToCSES(filename)  ScheduleClip.qml:172
//   AppCentral.scheduleManager.scheduleIO.importCSES()            Home.qml:64
//   AppCentral.scheduleManager.scheduleIO.importCW1()             Home.qml:83
//
// C++ 仓库的 QML 契约类是 src/core/schedule/ScheduleIO.h（方法名/签名逐字
// 一致，经 ScheduleManager::scheduleIO Q_PROPERTY 暴露）；本类承接 slots.py
// 的实际逻辑（文件对话框 + 转换编排），由 ScheduleIO 委托调用，避免在
// ScheduleManager 上再注册第二个全局对象——上游本就没有独立的
// "Convertor" 上下文属性（central.py 的 setContextProperty 列表无此项）。
class ConvertorBridge : public QObject
{
    Q_OBJECT
public:
    explicit ConvertorBridge(ScheduleManager *manager, QObject *parent = nullptr);

    // slots.py:16-35 exportToCSES(filename)：把 <schedules>/<filename>.json
    // 导出为 CSES YAML（文件对话框；用户取消/失败均返回 false，QML 弹错误提示）
    bool exportToCSES(const QString &filename);
    // slots.py:37-76 importCSES()：导入 CSES YAML 并应用为当前课表
    bool importCSES();
    // slots.py:78-115 importCW1()：导入 Class Widgets 1 课表并应用为当前课表
    bool importCW1();

private:
    // importCSES / importCW1 的公共流程（对话框文案、过滤器和目标后缀不同）
    bool importAndApply(const QString &sourceFormat, const QString &dialogTitle,
                        const QString &filter, const QString &destSuffix);

    ScheduleManager *m_manager = nullptr;
};
