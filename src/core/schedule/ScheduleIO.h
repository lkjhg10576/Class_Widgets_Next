#pragma once

#include <QObject>
#include <QString>

class ConvertorBridge;
class ScheduleManager;

// 对应上游 core/convertor/slots.py ScheduleIO（QML 经
// AppCentral.scheduleManager.scheduleIO.* 调用，仅编辑器页使用）。
//
// 本类是 QML 契约面（三个 Q_INVOKABLE 与上游 @Slot 名称/签名逐字一致），
// slots.py 的实际逻辑（文件对话框 + 转换编排）由 core/convertor 下的
// ConvertorBridge 承接（对应 slots.py 的实现本体），转换核心为
// ScheduleConverter（对应 converter.py）。
class ScheduleIO : public QObject
{
    Q_OBJECT
public:
    explicit ScheduleIO(ScheduleManager *manager, QObject *parent = nullptr);

    // slots.py:16-35 exportToCSES(filename)：导出为 CSES YAML
    Q_INVOKABLE bool exportToCSES(const QString &filename);
    // slots.py:37-76 importCSES()：导入 CSES YAML
    Q_INVOKABLE bool importCSES();
    // slots.py:78-115 importCW1()：导入 Class Widgets 1 课表
    Q_INVOKABLE bool importCW1();

private:
    ScheduleManager *m_manager = nullptr;
    ConvertorBridge *m_bridge = nullptr; // slots.py 逻辑实现（生命周期随本对象）
};
