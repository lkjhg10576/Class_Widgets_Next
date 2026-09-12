#include "ThemeLoader.h"

#include "../AppPaths.h"
#include "../Logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QVersionNumber>

namespace {

// 上游 src/themes/__init__.py BUILTIN_THEMES（路径改为 C++ 运行时布局）：
//   default 的 path 是整个 src/qml（DEFAULT_THEME = QML_PATH）；
//   其余内置主题位于 <root>/src/themes/<dirName>。
//   preview 统一落在 <root>/assets/images/themes/<dirName|default>.png。
struct BuiltinTheme
{
    const char *id;
    const char *name;        // 经 QCoreApplication::translate("Theme", ...) 动态翻译（loader.py:56）
    const char *description; // 同上（loader.py:57）
    const char *dirName;     // nullptr 表示 default 主题（path = src/qml）
    const char *color;
};

const BuiltinTheme kBuiltinThemes[] = {
    { "com.classwidgets.default", "Default",        "Class Widgets Builtin Default Theme",   nullptr,   "#4099b2" },
    { "com.classwidgets.cw1",     "Class Widgets 1", "Class Widgets 1 Classic Theme",         "cw1",     "#58CED7" },
    { "com.classwidgets.win10",   "Windows 10",      "Windows 10 Fluent 1 Style Theme",       "win10",   "#0078d7" },
    { "com.classwidgets.material","Material You",    "Material You 3 Style Theme",            "material","#6750A4" },
    { "com.classwidgets.vista",   "Vista",           "Windows Vista Skeuomorphic Style Theme","vista",   "#4F95AE" },
};

// loader.py:106-129 _load_external_meta：读取并校验单个外部主题的 cwtheme.json。
// 校验失败/文件缺失返回空 map（调用方跳过）。
QVariantMap loadExternalMeta(const QFileInfo &themeDir)
{
    const QString metaPath = themeDir.absoluteFilePath() + QStringLiteral("/cwtheme.json");
    QFile file(metaPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) {
        cwn::Log::error(QStringLiteral("Failed to load theme meta: %1").arg(metaPath));
        return {};
    }
    QVariantMap meta = doc.object().toVariantMap();

    // loader.py:131-138 _validate_meta：必填键 id/name/version/api_version/author
    const QStringList required = { QStringLiteral("id"), QStringLiteral("name"),
                                   QStringLiteral("version"), QStringLiteral("api_version"),
                                   QStringLiteral("author") };
    for (const QString &key : required) {
        if (meta.value(key).toString().isEmpty()) {
            cwn::Log::warn(QStringLiteral("Theme meta missing '%1': %2").arg(key, metaPath));
            return {};
        }
    }

    meta.insert(QStringLiteral("_type"), QStringLiteral("external"));
    meta.insert(QStringLiteral("_path"), themeDir.absoluteFilePath());

    // loader.py:120-124：preview 相对主题目录；不存在置空
    const QString preview = meta.value(QStringLiteral("preview")).toString();
    if (!preview.isEmpty()) {
        const QString previewPath = themeDir.absoluteFilePath() + QLatin1Char('/') + preview;
        meta.insert(QStringLiteral("preview"),
                    QFileInfo::exists(previewPath) ? QVariant::fromValue(QUrl::fromLocalFile(previewPath))
                                                   : QVariant());
    } else {
        meta.insert(QStringLiteral("preview"), QVariant());
    }
    return meta;
}

// namespace

// 头文件声明了带默认参的构造函数，但主体缺失导致 LNK2019；此处补上
// （无状态类：所有成员均有类内初始化，构造仅挂 parent）
ThemeLoader::ThemeLoader(QObject *parent)
    : QObject(parent)
{
}

bool ThemeLoader::isApiCompatible(const QString &themeApiVersion)
{
    // loader.py:29-31：空串或 "*" 视为任意版本
    const QString spec = themeApiVersion.trimmed();
    if (spec.isEmpty() || spec == QLatin1String("*"))
        return true;

    // 打包版主机的 API 版本（PEP 440 Version("2.0.0")）
    static const QVersionNumber appVersion = QVersionNumber::fromString(appApiVersion());

    // SpecifierSet 保守子集：逗号分隔的 ==/!=/>=/<=/>/</~=
    const QStringList clauses = spec.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &clause : clauses) {
        const QString trimmed = clause.trimmed();

        QString op;
        static const char *kOperators[] = { "==", "!=", ">=", "<=", "~=", ">", "<" };
        for (const char *candidate : kOperators) {
            if (trimmed.startsWith(QLatin1String(candidate))) {
                op = QLatin1String(candidate);
                break;
            }
        }
        if (op.isEmpty()) {
            cwn::Log::debug(QStringLiteral("Version check failed. Theme requirement: %1, "
                                           "Host version: %2. Error: unsupported specifier")
                                .arg(spec, appApiVersion()));
            return false; // loader.py:37-42：解析异常按不兼容处理
        }

        const QVersionNumber required =
            QVersionNumber::fromString(trimmed.mid(op.size()).trimmed());
        if (required.isNull()) {
            cwn::Log::debug(QStringLiteral("Version check failed. Theme requirement: %1, "
                                           "Host version: %2. Error: invalid version")
                                .arg(spec, appApiVersion()));
            return false;
        }

        const int cmp = QVersionNumber::compare(appVersion, required);
        bool ok = false;
        if (op == QLatin1String("=="))
            ok = cmp == 0;
        else if (op == QLatin1String("!="))
            ok = cmp != 0;
        else if (op == QLatin1String(">="))
            ok = cmp >= 0;
        else if (op == QLatin1String("<="))
            ok = cmp <= 0;
        else if (op == QLatin1String(">"))
            ok = cmp > 0;
        else if (op == QLatin1String("<"))
            ok = cmp < 0;
        else if (op == QLatin1String("~=")) {
            // PEP 440 兼容释放：~=V.N(...) → >=V.N(...) 且 <V.N前缀+1
            if (required.segmentCount() < 2)
                return false;
            QList<int> upperSegments;
            for (int i = 0; i < required.segmentCount() - 1; ++i)
                upperSegments.append(required.segmentAt(i));
            upperSegments.last() += 1;
            const QVersionNumber upperBound(upperSegments);
            ok = QVersionNumber::compare(appVersion, required) >= 0
                 && QVersionNumber::compare(appVersion, upperBound) < 0;
        }
        if (!ok)
            return false;
    }
    return true;
}

QVariantList ThemeLoader::scanThemes() const
{
    QVariantList metas;
    const AppPaths &paths = AppPaths::instance();

    // ---- 内置主题（权威来源，无兜底；loader.py:51-86）----
    cwn::Log::info(QStringLiteral("Loading built-in themes"));
    for (const BuiltinTheme &builtin : kBuiltinThemes) {
        QVariantMap meta;
        meta.insert(QStringLiteral("id"), QString::fromLatin1(builtin.id));
        meta.insert(QStringLiteral("name"), QCoreApplication::translate("Theme", builtin.name));
        meta.insert(QStringLiteral("description"),
                    QCoreApplication::translate("Theme", builtin.description));
        meta.insert(QStringLiteral("author"), QStringLiteral("Class Widgets Official"));
        meta.insert(QStringLiteral("version"), QStringLiteral("1.0.0"));
        meta.insert(QStringLiteral("api_version"), QStringLiteral("*"));
        meta.insert(QStringLiteral("color"), QString::fromLatin1(builtin.color));
        meta.insert(QStringLiteral("_type"), QStringLiteral("builtin"));

        const QString dirName = builtin.dirName ? QString::fromLatin1(builtin.dirName) : QString();
        const QString themePath = dirName.isEmpty()
                                      ? paths.qmlRoot()
                                      : paths.src() + QStringLiteral("/themes/") + dirName;
        meta.insert(QStringLiteral("_path"), themePath);
        meta.insert(QStringLiteral("_compatible"),
                    isApiCompatible(meta.value(QStringLiteral("api_version")).toString()));

        // loader.py:64-84：preview 转为 QUrl（文件缺失置空并告警）
        const QString previewFile = paths.assetsRoot() + QStringLiteral("/images/themes/")
                                    + (dirName.isEmpty() ? QStringLiteral("default") : dirName)
                                    + QStringLiteral(".png");
        if (QFileInfo::exists(previewFile)) {
            meta.insert(QStringLiteral("preview"), QUrl::fromLocalFile(previewFile));
        } else {
            cwn::Log::warn(QStringLiteral("Preview file does not exist: %1").arg(previewFile));
            meta.insert(QStringLiteral("preview"), QVariant());
        }
        metas.append(meta);
    }

    // ---- 外部主题（loader.py:88-104）----
    const QString externalPath = paths.themesRoot();
    if (!QFileInfo::exists(externalPath))
        QDir().mkpath(externalPath); // loader.py:90-91：缺失则创建
    cwn::Log::info(QStringLiteral("Scanning external themes: %1").arg(externalPath));

    const QDir themesDir(externalPath);
    const QFileInfoList entries = themesDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &dirInfo : entries) {
        cwn::Log::info(QStringLiteral("Found external theme directory: %1")
                           .arg(dirInfo.absoluteFilePath()));
        QVariantMap meta = loadExternalMeta(dirInfo);
        if (meta.isEmpty())
            continue;
        meta.insert(QStringLiteral("_compatible"),
                    isApiCompatible(meta.value(QStringLiteral("api_version")).toString()));
        metas.append(meta);
    }

    cwn::Log::info(QStringLiteral("Total themes loaded: %1").arg(metas.size()));
    return metas;
}
