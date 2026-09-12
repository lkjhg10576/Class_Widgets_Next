#pragma once

#include <QObject>
#include <QString>

class QTranslator;
class ConfigStore;

// 对应上游 src/core/utils/translator.py AppTranslator（QML 经
// AppCentral.translator 上下文属性访问；上游 17 处引用，grep 实测消费面：
// getLanguage 6 / language 4 / getSystemLanguage 4 / setLanguage 3）。
//
// 加载 app/assets/locales/<语言>.qm（QTranslator），配置键 "locale.language"
// （translator.py:24/38/60；config/model.py:112 默认 = QLocale::system().name()）。
// 构造时按当前配置加载一次，对应 central.py:447 启动期的
// app_translator.setLanguage(configs.locale.language)。
class Translator : public QObject
{
    Q_OBJECT
    // translator.py:26-28 @Property(str, notify=languageChanged) language
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)

public:
    explicit Translator(ConfigStore *configs, QObject *parent = nullptr);
    ~Translator() override;

    // translator.py:21-24 getLanguage
    Q_INVOKABLE QString getLanguage() const;
    // translator.py:26-28 language 属性读取
    QString language() const;
    // translator.py:30-32 getSystemLanguage
    Q_INVOKABLE QString getSystemLanguage() const;
    // translator.py:34-62 setLanguage（sample: zh_CN; en_US）
    Q_INVOKABLE void setLanguage(const QString &localeName);
    // translator.py:65-68 tr：QML 访问翻译的接口
    Q_INVOKABLE QString tr(const QString &context, const QString &sourceText) const;

signals:
    // translator.py:12 languageChanged = Signal(str)
    void languageChanged(const QString &language);

private:
    // translator.py:53/59 的加载与安装（新翻译器后安装，Qt 逆序查询使应用
    // 翻译优先于其它目录，见 translator.py:57-58 注释）
    void swapTranslator(QTranslator *replacement);

    ConfigStore *m_configs = nullptr;
    QTranslator *m_translator = nullptr;
};
