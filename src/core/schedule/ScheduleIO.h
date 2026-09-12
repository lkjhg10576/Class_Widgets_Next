#pragma once

#include <QObject>
#include <QString>

class ScheduleManager;

// 对应上游 core/convertor/slots.py ScheduleIO（QML 经
// AppCentral.scheduleManager.scheduleIO.* 调用）。
//
// 三个转换方法依赖 core/convertor/converter.py（599 行，含 YAML 解析），
// 该文件不在本次 M2 移植范围内（任务范围仅 8 个上游文件），因此当前
// 一律记录警告并返回 false；QML 侧会按失败分支给出提示。待移植
// converter.py 后补齐实现（见最终报告"已知简化点"）。
class ScheduleIO : public QObject
{
    Q_OBJECT
public:
    explicit ScheduleIO(ScheduleManager *manager, QObject *parent = nullptr);

    // slots.py:12-32 exportToCSES(filename)：导出为 CSES YAML
    Q_INVOKABLE bool exportToCSES(const QString &filename);
    // slots.py:34-69 importCSES()：导入 CSES YAML
    Q_INVOKABLE bool importCSES();
    // slots.py:71-110 importCW1()：导入 Class Widgets 1 课表
    Q_INVOKABLE bool importCW1();

private:
    ScheduleManager *m_manager = nullptr;
};
