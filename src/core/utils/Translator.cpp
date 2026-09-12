#include "Translator.h"

#include "../AppPaths.h"
#include "../ConfigStore.h"
#include "../Logger.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QLocale>
#include <QTranslator>
#include <optional>

namespace {

// translator.py:38 锁定键名
constexpr char kLanguageKey[] = "locale.language";

} // namespace

Translator::Translator(ConfigStore *configs, QObject *parent)
    : QObject(parent)
    , m_configs(configs)
{
    // 对应 central.py:447：启动期按配置加载一次语言
    setLanguage(language());
}

Translator::~Translator()
{
    // 析构时卸载翻译器（上游进程退出无需此步，C++ 显式清理）
    if (m_translator) {
        if (QCoreApplication *app = QCoreApplication::instance())
            app->removeTranslator(m_translator);
        m_translator->deleteLater();
        m_translator = nullptr;
    }
}

QString Translator::getLanguage() const
{
    // translator.py:21-24 return configs.locale.language
    return language();
}

QString Translator::language() const
{
    // translator.py:26-28；model.py:112 默认 = QLocale::system().name()
    //（ConfigStore.cpp:502 的默认树已按此填值，这里再兜底一次防缺键）
    if (m_configs) {
        const auto value = m_configs->value(QLatin1String(kLanguageKey));
        const QString language = value.value_or(QJsonValue()).toString();
        if (!language.isEmpty())
            return language;
    }
    return QLocale::system().name();
}

QString Translator::getSystemLanguage() const
{
    // translator.py:30-32
    return QLocale::system().name();
}

void Translator::setLanguage(const QString &localeName)
{
    // translator.py:34-62 setLanguage（sample: zh_CN; en_US）
    if (m_configs && m_configs->isKeyLocked(QLatin1String(kLanguageKey))) {
        // translator.py:37-39
        cwn::Log::warn(QStringLiteral(
                           "Attempt to modify locked config key: %1. Blocked.")
                           .arg(QLatin1String(kLanguageKey)));
        return;
    }

    // translator.py:40-43：语言文件缺失仅告警（上游 fallback 行被注释，保持语义，
    // 例：zh_CN/zh_HK 暂无 .qm 时保持英文源文显示）
    const QString langPath = AppPaths::instance().assetsRoot()
        + QStringLiteral("/locales/") + localeName + QStringLiteral(".qm");
    if (!QFileInfo::exists(langPath)) {
        cwn::Log::warn(QStringLiteral("Language file %1 not found. Fallback to default (en_US)")
                           .arg(langPath));
    }

    swapTranslator(new QTranslator(this));

    // translator.py:54 QLocale.setDefault(QLocale(locale_name))
    QLocale::setDefault(QLocale(localeName));

    if (m_translator && m_translator->load(langPath)) {
        // translator.py:59：后安装使应用翻译优先生效（Qt 逆序查询）
        if (QCoreApplication *app = QCoreApplication::instance())
            app->installTranslator(m_translator);
        cwn::Log::info(QStringLiteral("Translator loaded: %1").arg(localeName));
    } else if (m_translator) {
        cwn::Log::warn(QStringLiteral("Failed to load translation: %1").arg(langPath));
        m_translator->deleteLater();
        m_translator = nullptr;
    }

    // translator.py:60 configs.locale.language = locale_name
    //（锁定检查已做，setInternal 直接写盘并触发 dataChanged）
    if (m_configs)
        m_configs->setInternal(QLatin1String(kLanguageKey), QJsonValue(localeName));

    // translator.py:61 languageChanged.emit(locale_name)
    emit languageChanged(localeName);
}

QString Translator::tr(const QString &context, const QString &sourceText) const
{
    // translator.py:65-68：QApplication.translate(context, source_text)
    return QCoreApplication::translate(context.toUtf8().constData(),
                                       sourceText.toUtf8().constData());
}

void Translator::swapTranslator(QTranslator *replacement)
{
    // translator.py:45-49：先卸载旧翻译器再换新（RinUITranslator 为 Python 版
    // RinUI 包组件（translator.py:6/51），C++ 侧 vendored RinUI QML 无对应物，
    // 仅保留 QTranslator 的换装与安装顺序语义）
    QCoreApplication *app = QCoreApplication::instance();
    if (m_translator) {
        if (app)
            app->removeTranslator(m_translator);
        m_translator->deleteLater();
    }
    m_translator = replacement;
}
