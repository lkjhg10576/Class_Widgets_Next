#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <QUrl>
#include <QVariantList>

class ConfigStore;

// 随机点名扩展服务（阶段 C1，extensions-feature-plan §6）：
// 名单存储（配置键 extensions.roll_call.names，元素 {name, weight(-100~100)}）、
// 不放回加权抽取、重复策略（extensions.roll_call.avoid_repeat =
// "single"|"session"）与 TXT 名单解析。经上下文名 "RollCall" 暴露给
// RollCallFloat/RollCallResult/设置页三处 QML。
//
// 权重语义（§10.5 默认假设）：有效权重 = 1 + weight/100 ∈ [0, 2]；
// -100 永不抽中、0 与他人等权、+100 概率翻倍。
//
// 线程模型：全部逻辑在 GUI 线程同步执行（无网络/定时器），窗口与 QML 直接调用。
class RollCallService : public QObject
{
    Q_OBJECT
    // 最近一次 draw() 的结果（元素 {name, weight} 的 QVariantList），
    // NOTIFY 用 drawCompleted —— 结果窗口/悬浮面板绑定即得最新名单
    Q_PROPERTY(QVariantList lastDraw READ lastDraw NOTIFY drawCompleted)

public:
    explicit RollCallService(ConfigStore *configs, QObject *parent = nullptr);

    // --- QML 契约 ---

    // 不放回加权抽取 count 人；返回本轮结果（元素 {name, weight}，选 QVariantMap
    // 而非纯字符串是为 QML 渲染留扩展位，如未来按权重标注）。
    // 边界：count<=0 或名单为空 → 返回空列表；count 超过可抽人数 → 抽出全部
    // 并在结果长度中体现（QML 侧据此提示）。session 模式下本轮结果并入会话
    // 排除名单（关闭结果窗口经 clearSession() 清空）。
    Q_INVOKABLE QVariantList draw(int count);

    // 读 UTF-8 txt（每行一个名字；跳过空白行、文件内去重、剥 BOM），返回解析
    // 出的名单 [{name, weight:0}]。无副作用：导入结果由设置页合并去重后经
    // addName 写回 —— 服务保持纯解析，避免选错文件直接污染名单。
    // QML 无法读本地文件，导入必须走 C++（§6 C1）。
    Q_INVOKABLE QVariantList importNamesFromUrl(const QUrl &url);

    // 名单编辑：全部写回 extensions.roll_call.names 并立即 save()
    // （用户显式操作，对齐 ExtensionManager::setEnabled 的即时落盘语义）
    Q_INVOKABLE bool addName(const QString &name);
    Q_INVOKABLE bool updateName(int index, const QString &name, int weight);
    Q_INVOKABLE bool removeName(int index);
    Q_INVOKABLE void clearNames();

    // 清空会话排除名单（"session" 模式运行期状态，不落盘；结果窗口关闭时调用）
    Q_INVOKABLE void clearSession();

    // 最近一次 draw() 请求的人数：结果窗口与 lastDraw().length 对比后提示
    // "名单人数不足，已全部抽出"（N 超员时 draw 只返回实际可抽人数）
    Q_INVOKABLE int lastRequested() const { return m_lastRequested; }

    QVariantList lastDraw() const { return m_lastDraw; }

signals:
    void drawCompleted(const QVariantList &result);
    void namesChanged();

private:
    // 读当前名单（sanitize 已保证元素形状，此处仍做防御性过滤/钳位）
    QVariantList readNames() const;
    // 写回 + 落盘 + namesChanged
    void writeNames(const QVariantList &names);
    QString avoidRepeatMode() const;

    ConfigStore *m_configs = nullptr;
    QVariantList m_lastDraw;
    int m_lastRequested = 0;
    // "session" 模式的会话排除集合（按名字，名单本身保证名字唯一）
    QSet<QString> m_sessionPicked;
};
