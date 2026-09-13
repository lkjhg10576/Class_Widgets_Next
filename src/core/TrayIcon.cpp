#include "TrayIcon.h"

// B4：win32 原生托盘（Shell_NotifyIcon + HMENU 弹出菜单），详见头文件说明。
#include <QtCore/qt_windows.h> // 自带 NOMINMAX，防 min/max 宏污染 Qt 头
#include <shellapi.h>

#include "AppPaths.h"
#include "Logger.h"

#include <QCoreApplication>
#include <QCursor>
#include <QIcon>
#include <QImage>

namespace {

constexpr UINT kTrayCallbackMsg = WM_APP + 1;
constexpr UINT kTrayId = 1;
const wchar_t kWindowClassName[] = L"ClassWidgetsNext_TrayHost";

const wchar_t *asWStr(const QString &s)
{
    return reinterpret_cast<const wchar_t *>(s.utf16());
}

// 按目标缓冲区容量拷贝并截断（气泡标题 64 字符 / 正文 256 字符上限为系统约束）
void copyTrayString(wchar_t *dst, size_t cap, const QString &src)
{
    wcsncpy_s(dst, cap, asWStr(src), _TRUNCATE);
}

} // namespace

TrayIcon::TrayIcon(QObject *parent)
    : QObject(parent)
{
    if (!registerWindowClass()) {
        cwn::Log::error(QStringLiteral("Tray: failed to register window class"));
        return;
    }

    m_hwnd = CreateWindowExW(0, kWindowClassName, L"", WS_OVERLAPPED, 0, 0, 0, 0,
                             nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!m_hwnd) {
        cwn::Log::error(QStringLiteral("Tray: failed to create host window"));
        return;
    }
    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    m_icon = loadTrayHIcon();
    if (!m_icon) {
        cwn::Log::error(QStringLiteral("Tray icon not found: %1")
                            .arg(AppPaths::instance().assetsRoot()
                                 + QStringLiteral("/images/tray_icon.png")));
        return;
    }

    // 菜单项图标栅格化为小图标尺寸的 32bpp 位图（原生菜单 hbmpItem）
    const QString iconRoot = AppPaths::instance().assetsRoot()
        + QStringLiteral("/images/icons/");
    m_bmpSettings = menuIconBitmap(iconRoot + QStringLiteral("cw2_settings.png"));
    m_bmpEditor = menuIconBitmap(iconRoot + QStringLiteral("cw2_editor.png"));
    m_bmpTutorial = menuIconBitmap(iconRoot + QStringLiteral("smart_teach.svg"));

    // explorer 重启时广播 TaskbarCreated，需重新添加图标
    m_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

    if (addIcon()) {
        cwn::Log::info(QStringLiteral("Tray icon initialized"));
    } else {
        cwn::Log::error(QStringLiteral("Tray: Shell_NotifyIcon(NIM_ADD) failed"));
    }
}

TrayIcon::~TrayIcon()
{
    cleanup();
}

bool TrayIcon::registerWindowClass()
{
    WNDCLASSEXW wc = {};
    if (GetClassInfoExW(GetModuleHandleW(nullptr), kWindowClassName, &wc))
        return true; // 已注册（单实例应用，仅 TrayIcon 使用）
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &TrayIcon::trayWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kWindowClassName;
    return RegisterClassExW(&wc) != 0;
}

HICON TrayIcon::loadTrayHIcon()
{
    const QString iconPath = AppPaths::instance().assetsRoot()
        + QStringLiteral("/images/tray_icon.png");
    QImage img(iconPath);
    if (img.isNull())
        return nullptr;
    const int cx = GetSystemMetrics(SM_CXICON);
    const int cy = GetSystemMetrics(SM_CYICON);
    if (img.width() != cx || img.height() != cy)
        img = img.scaled(cx, cy, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return img.toHICON();
}

HBITMAP TrayIcon::menuIconBitmap(const QString &assetPath)
{
    const int cx = GetSystemMetrics(SM_CXSMICON);
    const int cy = GetSystemMetrics(SM_CYSMICON);
    QImage img = QIcon(assetPath).pixmap(cx, cy).toImage(); // png/svg 统一经 QIcon 引擎
    if (img.isNull())
        return nullptr;
    if (img.width() != cx || img.height() != cy)
        img = img.scaled(cx, cy, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return img.convertToFormat(QImage::Format_ARGB32_Premultiplied).toHBITMAP();
}

bool TrayIcon::addIcon()
{
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = m_hwnd;
    nid.uID = kTrayId;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = kTrayCallbackMsg;
    nid.hIcon = m_icon;
    copyTrayString(nid.szTip, _countof(nid.szTip), QStringLiteral("Class Widgets Next"));
    m_added = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
    return m_added;
}

void TrayIcon::cleanup()
{
    if (m_added) {
        NOTIFYICONDATAW nid = {};
        nid.cbSize = sizeof(nid);
        nid.hWnd = m_hwnd;
        nid.uID = kTrayId;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        m_added = false;
    }
    for (HBITMAP *bmp : { &m_bmpSettings, &m_bmpEditor, &m_bmpTutorial }) {
        if (*bmp) {
            DeleteObject(*bmp);
            *bmp = nullptr;
        }
    }
    if (m_icon) {
        DestroyIcon(m_icon);
        m_icon = nullptr;
    }
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

void TrayIcon::showEditNotification(const QString &title, const QString &text)
{
    if (!m_added)
        return;
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = m_hwnd;
    nid.uID = kTrayId;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_INFO; // 旧 QSystemTrayIcon::Information 语义
    copyTrayString(nid.szInfoTitle, _countof(nid.szInfoTitle), title);
    copyTrayString(nid.szInfo, _countof(nid.szInfo), text);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void TrayIcon::retranslate()
{
    // 菜单每次打开时按当前语言重建，无需原地刷新
}

LRESULT CALLBACK TrayIcon::trayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto *self = reinterpret_cast<TrayIcon *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self)
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    return self->handleMessage(msg, wParam, lParam);
}

LRESULT TrayIcon::handleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    Q_UNUSED(wParam);
    if (msg == m_taskbarCreatedMsg) {
        // explorer 重启：重挂图标（先 DELETE 清理可能残留的旧条目）
        NOTIFYICONDATAW nid = {};
        nid.cbSize = sizeof(nid);
        nid.hWnd = m_hwnd;
        nid.uID = kTrayId;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        addIcon();
        return 0;
    }
    if (msg != kTrayCallbackMsg)
        return DefWindowProcW(m_hwnd, msg, wParam, lParam);

    // 旧实现：activated 任意 reason 都发 togglePanel(QCursor::pos())。
    // 用户反馈（B4 实测）：右键若先唤起 QML TrayPanel 再弹原生菜单，面板会被
    // 菜单抢焦点立即收回，产生"闪一下大窗口"的观感——右键改为只弹菜单；
    // 左键/中键/双击保持 togglePanel 语义。
    switch (lParam) {
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_MBUTTONUP:
        emit togglePanel(QCursor::pos());
        break;
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
        showContextMenu();
        break;
    default:
        break;
    }
    return 0;
}

void TrayIcon::showContextMenu()
{
    if (!m_hwnd)
        return;

    HMENU menu = CreatePopupMenu();
    if (!menu)
        return;

    const auto addItem = [menu](UINT id, const QString &text, HBITMAP bitmap) {
        MENUITEMINFOW mi = {};
        mi.cbSize = sizeof(mi);
        mi.fMask = MIIM_ID | MIIM_STRING | MIIM_STATE;
        mi.wID = id;
        mi.dwTypeData = const_cast<LPWSTR>(asWStr(text)); // InsertMenuItem 会拷贝字符串
        mi.fState = MFS_ENABLED;
        if (bitmap) {
            mi.fMask |= MIIM_BITMAP;
            mi.hbmpItem = bitmap;
        }
        InsertMenuItemW(menu, GetMenuItemCount(menu), TRUE, &mi);
    };

    // B4 托盘菜单结构（用户反馈）：调休在换课上方、切换课程表独立弹出界面、
    // 退出上方加重启、砍"关于"（设置窗口首页即关于）
    addItem(CmdOpenSettings, QCoreApplication::translate("TrayIcon", "Open Settings"),
            m_bmpSettings);
    addItem(CmdOpenEditor, QCoreApplication::translate("TrayIcon", "Schedule Editor"),
            m_bmpEditor);
    addItem(CmdRescheduleDay, QCoreApplication::translate("TrayIcon", "Reschedule Day"),
            nullptr);
    addItem(CmdOpenClassSwap, QCoreApplication::translate("TrayIcon", "Class Swap"), nullptr);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    addItem(CmdSwitchSchedule, QCoreApplication::translate("TrayIcon", "Switch Schedule"),
            nullptr);
    addItem(CmdMiniMode, QCoreApplication::translate("TrayIcon", "Mini Mode"), nullptr);
    addItem(CmdEditMode, QCoreApplication::translate("TrayIcon", "Toggle Edit Mode"), nullptr);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    addItem(CmdTutorial, QCoreApplication::translate("TrayIcon", "Tutorial"), m_bmpTutorial);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    addItem(CmdRestart, QCoreApplication::translate("TrayIcon", "Restart"), nullptr);
    addItem(CmdQuit, QCoreApplication::translate("TrayIcon", "Quit"), nullptr);

    POINT pt = {};
    GetCursorPos(&pt);
    // KB135788：弹出前置前台 + 弹出后补 WM_NULL，保证点击菜单外能正常收起
    SetForegroundWindow(m_hwnd);
    const int cmd = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                                     pt.x, pt.y, m_hwnd, nullptr);
    DestroyMenu(menu);
    PostMessageW(m_hwnd, WM_NULL, 0, 0);

    switch (cmd) {
    case CmdOpenSettings:
        emit openSettingsRequested();
        break;
    case CmdOpenEditor:
        emit openEditorRequested();
        break;
    case CmdRescheduleDay:
        emit rescheduleDayRequested();
        break;
    case CmdOpenClassSwap:
        emit openClassSwapRequested();
        break;
    case CmdSwitchSchedule:
        emit switchScheduleRequested();
        break;
    case CmdMiniMode:
        emit miniModeRequested();
        break;
    case CmdEditMode:
        emit editModeRequested();
        break;
    case CmdTutorial:
        emit openTutorialRequested();
        break;
    case CmdRestart:
        emit restartRequested();
        break;
    case CmdQuit:
        cwn::Log::info(QStringLiteral("Quit requested from tray"));
        QCoreApplication::quit();
        break;
    default:
        break; // 用户取消
    }
}
