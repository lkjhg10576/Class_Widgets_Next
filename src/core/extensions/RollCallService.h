#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <QUrl>
#include <QVariantList>

class ConfigStore;
class NotificationProvider;
class QTimer;

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
    // 出的名单 [{name, weight:0}]。无副作用：解析结果由设置页经下面的
    // mergeNames 批量合并写回 —— 服务保持纯解析，避免选错文件直接污染名单。
    // QML 无法读本地文件，导入必须走 C++（§6 C1）。
    // four-plugins 一期扩展：docx（zip 解 word/document.xml 取 <w:t>，按
    // <w:p>/<w:br> 转行）+ 多编码（utf-8-sig/utf-16/gbk）+ `#` 整行注释 +
    // 去序号（`^\s*(\d+[.．、]|\(\d+\)|第\d+名)`）+ 行内分割（`,，;；、\t双空格`）。
    Q_INVOKABLE QVariantList importNamesFromUrl(const QUrl &url);

    // 批量合并名单（TXT 导入路径的写入口：importNamesFromUrl 解析结果原样传入）。
    // entries 元素接受 {name, weight} 映射（解析结果形状）或裸字符串；与现有名单
    // 及批内去重（空名/重名跳过），weight 缺省 0、越界钳位 [-100,100]；
    // added > 0 时单次写回 + 落盘 + 一次 namesChanged，返回实际新增人数。
    // 为什么必须批量：逐名 addName 会让每次合入都触发整树 dataChanged → 设置页
    // Repeater 全量重建（每人一张重卡片）+ 整份 configs.json 落盘，导入 N 人到
    // 现有 M 人名单是 O(N·(M+N)) 次委托创建与 N 次全量磁盘写，大名单导入时
    // 内存暴涨、界面假死。批量入口把写回/落盘/信号各收敛为一次。
    Q_INVOKABLE int mergeNames(const QVariantList &entries);

    // 名单编辑：全部写回 extensions.roll_call.names 并立即 save()
    // （用户显式操作，对齐 ExtensionManager::setEnabled 的即时落盘语义）
    Q_INVOKABLE bool addName(const QString &name);
    Q_INVOKABLE bool updateName(int index, const QString &name, int weight);
    Q_INVOKABLE bool removeName(int index);
    Q_INVOKABLE void clearNames();

    // 清空会话排除名单（"session" 模式运行期状态，不落盘；结果窗口关闭时调用）
    Q_INVOKABLE void clearSession();

    // ── four-plugins 一期新增 ──
    // 权重清零（全部回 0=等权，单次写回 + 落盘 + namesChanged；空名单时无操作）
    Q_INVOKABLE void resetWeights();
    // 试抽预览：与 draw() 同权重算法，但不改变会话排除名单、不更新 lastDraw、
    // 不发 drawCompleted（设置页“试抽”按钮用，结果只返回不污染状态）
    Q_INVOKABLE QVariantList testDraw(int count);
    // 上游 1-100（默认 100）→ Next -100~+100 近似迁移公式（§10.5 默认假设）：
    // w_new=(w_old-100)*2/100。mergeNames 对“全批为 1..100 且无负值”的上游遗留批量
    // 自动应用本公式；新范围值原样通过。UI 兼容显示：设置页滑杆仍按 -100~+100 渲染。
    static int migrateUpstreamWeight(int wOld);
    // 点名结果经灵动通知播报（停留 2-15s，由 extensions.roll_call.notify_duration
    // 配置，越界钳位）。播报前临时展开 hide 层、播后还原（four-plugins §4.2）：
    // 展开与还原都由本方法全权负责，还原计时器挂本服务（应用生命周期）——
    // 质检修正：原实现把还原 Timer 放在悬浮窗 QML，click_hide/播报期关窗路径
    // 下窗口先于定时器销毁，hide 层永久停在展开态。
    Q_INVOKABLE void announce(const QStringList &names);

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
    // 播报临时展开 hide 层的还原（见 announce 注释）：仅当 hide.state 仍处于
    // 播报展开态（false）且未锁定时才写回 true，尊重新播报与用户中途干预
    void restoreHiddenState();

    ConfigStore *m_configs = nullptr;
    QVariantList m_lastDraw;
    int m_lastRequested = 0;
    // "session" 模式的会话排除集合（按名字，名单本身保证名字唯一）
    QSet<QString> m_sessionPicked;
    // 点名播报通知来源（com.classwidgets.rollcall，灵动通知设置页可见/可关；
    // service=nullptr 即挂默认 NotificationService 实例）
    NotificationProvider *m_announceProvider = nullptr;
    // 播报临时展开 hide 层的还原定时器与未还原标记（懒创建，单实例 restart）
    QTimer *m_hideRestoreTimer = nullptr;
    bool m_hideRestorePending = false;
};
