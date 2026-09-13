#pragma once

#include "RinUiWindowBase.h"

#include <QRegion>
#include <QTimer>

class AppCentral;

// 对应上游 core/widgets/core.py 的 WidgetsWindow（T10）。
//
// 两个隐藏依赖必须照搬（M1 拆分文档 §1.3）：
// ① update_mask()：把全屏透明窗口的 mask 收缩到小组件实际矩形，
//    mask 为空时必须兜底 QRegion(0,0,1,1) —— 否则鼠标穿透/整屏遮挡；
// ② 鼠标悬停轮询 update_mouse_state()（A6 起 hover_fade 关闭即停，间隔 100ms；
//    M3 注释的"33ms 改事件驱动"中期方向保留）。
class WidgetsWindow : public RinUiWindowBase
{
    Q_OBJECT
public:
    explicit WidgetsWindow(AppCentral *central, QObject *parent = nullptr);

    // 启动小组件窗口（对应 WidgetsWindow.run）
    void run();

    bool isQmlReady() const { return m_qmlReady; }

    // C1 缓存纪律：辅助窗口关闭 → 主引擎（共享引擎）延迟 trim（脏检查节拍，
    // C1 起为 30s）。连续开关多个窗口只合并为一次 trim；trim 后无新脏即停表，
    // 不空转（A6 纪律）。
    void notifyAuxiliaryWindowReleased();

signals:
    void themeLoadFailed(const QString &themeId);

private slots:
    // 字符串式 connect（SIGNAL(geometryChanged())）要求它是真正的槽
    void scheduleMaskUpdate();

private:
    static constexpr int kMousePollIntervalMs = 100; // A6：原 33ms
    static constexpr int kTrimIntervalMs = 30 * 1000; // C1：脏驱动回落节拍（原 B3 5min）

    void onQmlReady(QObject *obj, const QUrl &objUrl);
    void onThemeChanged();
    void updateMask();
    void applyEmptyMask();
    void updateMouseState();
    void onTrimTick();

    AppCentral *m_central = nullptr;
    QUrl m_mainQmlUrl;
    QRegion m_interactiveRect;
    QTimer m_mouseTimer;
    QTimer m_trimTimer;
    bool m_trimDirty = false;
    bool m_maskUpdatePending = false;
    bool m_qmlReady = false;
    bool m_themeReloading = false;
    bool m_acceptsInput = true;
    bool m_hoverFade = false;
};
