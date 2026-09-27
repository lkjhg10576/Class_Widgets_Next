#pragma once

#include "../WeatherProvider.h"

// 小米天气 wtr-v3 数据源（移植自 NetSpeed-Dynamic src-tauri/src/weather.rs）。
// 数据源无需申请 key：appKey/sign 为该公开接口的固定常量（上游模块头原文声明）。
// 职责：城市搜索（location/city/search）+ 实况/今日预报/预警合并拉取（weather/all）。
// 本接口是 QML 数值契约的基准源；其余 Provider 把各自响应归一到同一形状。
class XiaomiProvider final : public WeatherProvider
{
    Q_OBJECT
public:
    explicit XiaomiProvider(QNetworkAccessManager *nam, QObject *parent = nullptr);

    QString id() const override { return QStringLiteral("xiaomi"); }
    bool isConfigured() const override { return true; } // 固定公开常量，无需配置
    void searchCity(const QString &keyword) override;
    void fetch(const CityInfo &city) override;
    void testConnection() override;
};
