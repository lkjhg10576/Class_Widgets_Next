#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

// 对应上游 src/core/themes/loader.py 的 ThemeLoader。
// 职责（loader.py:45-138）：
//   1. 扫描内置主题（上游 BUILTIN_THEMES 表，src/themes/__init__.py：default/cw1/win10/material/vista）
//   2. 扫描外部主题 <root>/themes/*/cwtheme.json（必填键校验，loader.py:131-138）
//   3. api_version 兼容性检查（PEP 440 SpecifierSet 的保守子集，loader.py:28-42）
// 产出 QVariantList，每项键与 model.py ThemeMeta 对齐：
//   id / name / description / version / api_version / author / color /
//   preview(QUrl|null) / _type(builtin|external) / _path / _compatible(bool)
class ThemeLoader : public QObject
{
    Q_OBJECT
public:
    explicit ThemeLoader(QObject *parent = nullptr);

    // loader.py:25 APP_API_VERSION
    static QString appApiVersion() { return QStringLiteral("2.0.0"); }

    // loader.py:28-42 is_compatible：接受 "*"、空串或逗号分隔的比较子句
    // （==/!=/>=/<=/>/</~=）；版本号解析失败按上游语义返回 false
    static bool isApiCompatible(const QString &themeApiVersion);

    // loader.py:48-104 scan_themes：内置 + 外部主题元数据列表
    QVariantList scanThemes() const;
};
