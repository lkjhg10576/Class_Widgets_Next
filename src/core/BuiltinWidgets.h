#pragma once

#include <QList>
#include <QObject>
#include <QVariantMap>

#include "WidgetsModel.h"

// 小组件注册入口的稳定接口（移植方案 §0.5.8 第 3 条纪律）：
// 现在只有内置实现；将来 QmlWidgetProvider（JS 插件）或
// SidecarWidgetProvider（Python 插件）都挂到这个接口上，
// 不要把 WidgetsModel 写死成只认内置表。
class IWidgetProvider
{
public:
    virtual ~IWidgetProvider() = default;
    virtual QList<WidgetDefinition> widgets() const = 0;
};

// 内置 6 个小组件的 backend（对应上游内置插件 cw_widgets 的 Plugin 实例）。
// QML 侧对 backend 的调用面实测只有 getDateTime()（Time.qml / Text.qml），
// sayHello() 对应 test.qml（测试文件，保留以对齐契约）。
class WidgetBackend : public QObject
{
    Q_OBJECT
public:
    explicit WidgetBackend(QObject *parent = nullptr);

    // 对应 cw_widgets getDateTime()：hour/minute/second 为 "02d" 字符串，
    // weekday 为 isoweekday（1=周一 … 7=周日）
    Q_INVOKABLE QVariantMap getDateTime() const;
    Q_INVOKABLE QString sayHello() const;
};

// 对应 src/plugins/cw_widgets/widgets.py 的 6 个注册项（字段逐字对齐）。
class BuiltinWidgetProvider : public IWidgetProvider
{
public:
    QList<WidgetDefinition> widgets() const override;
};
