#include "NativeFileDialog.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <wrl/client.h>

#include "Logger.h"

#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QVector>
#include <QWindow>

using Microsoft::WRL::ComPtr;

namespace {

// 对话框属主：取焦点窗口（设置页/托盘面板等按钮触发方），否则取首个可见顶层窗口。
// 与 QFileDialog(nullptr, ...) 使用活动窗口作 owner 的行为对齐，保证对话框
// 不会沉到全屏常驻主窗口之下。
HWND ownerHwnd()
{
    if (const QWindow *focused = QGuiApplication::focusWindow()) {
        if (focused->handle())
            return reinterpret_cast<HWND>(focused->winId());
    }
    for (const QWindow *w : QGuiApplication::topLevelWindows()) {
        if (w->isVisible() && w->handle())
            return reinterpret_cast<HWND>(w->winId());
    }
    return nullptr;
}

// "Desc (*.a *.b);;Desc2 (*.c)" → COMDLG_FILTERSPEC 数组
QVector<COMDLG_FILTERSPEC> parseFilter(const QString &filter,
                                       QVector<QPair<QString, QString>> &storage)
{
    const QStringList groups = filter.split(QStringLiteral(";;"), Qt::SkipEmptyParts);
    for (const QString &group : groups) {
        const qsizetype open = group.indexOf(QLatin1Char('('));
        const qsizetype close = group.lastIndexOf(QLatin1Char(')'));
        if (open <= 0 || close < open)
            continue;
        storage.append({ group.left(open).trimmed(),
                         group.mid(open + 1, close - open - 1).trimmed() });
    }
    if (storage.isEmpty())
        storage.append({ QStringLiteral("All Files (*.*)"), QStringLiteral("*.*") });

    QVector<COMDLG_FILTERSPEC> specs;
    specs.reserve(storage.size());
    for (const auto &entry : storage) {
        COMDLG_FILTERSPEC spec;
        spec.pszName = reinterpret_cast<PCWSTR>(entry.first.utf16());
        spec.pszSpec = reinterpret_cast<PCWSTR>(entry.second.utf16());
        specs.append(spec);
    }
    return specs;
}

void setStartDirectory(IFileDialog *dialog, const QString &dir)
{
    const QFileInfo info(dir);
    if (!info.isAbsolute())
        return;
    QString folder = info.isDir() ? QDir(dir).absolutePath() : info.absolutePath();
    if (folder.isEmpty())
        return;
    ComPtr<IShellItem> item;
    if (FAILED(SHCreateItemFromParsingName(reinterpret_cast<PCWSTR>(folder.utf16()),
                                           nullptr, IID_PPV_ARGS(&item))))
        return;
    // 已存在的目录直接定位（对齐 QFileDialog dir 行为）；失败则忽略保持系统默认
    dialog->SetFolder(item.Get());
}

// 模态执行并取回结果；取消返回空串（HRESULT_FROM_WIN32(ERROR_CANCELLED)）
QString runDialog(bool save, const QString &title, const QString &dir,
                  const QString &filter)
{
    // Qt GUI 线程已按 STA 初始化 COM；此处防御性初始化，S_FALSE/RPC_E_CHANGED_MODE 均可继续
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool comOwned = SUCCEEDED(hr);
    if (FAILED(hr)) {
        cwn::Log::error(QStringLiteral("NativeFileDialog: COM init failed 0x%1")
                            .arg(uint(hr), 8, 16, QLatin1Char('0')));
        return {};
    }
    struct ComGuard {
        bool owned;
        ~ComGuard()
        {
            if (owned)
                CoUninitialize();
        }
    } guard{ comOwned };

    ComPtr<IFileDialog> dialog;
    hr = CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr,
                          CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(hr)) {
        cwn::Log::error(QStringLiteral("NativeFileDialog: CoCreateInstance failed 0x%1")
                            .arg(uint(hr), 8, 16, QLatin1Char('0')));
        return {};
    }

    DWORD options = 0;
    dialog->GetOptions(&options);
    options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
    if (save)
        options |= FOS_OVERWRITEPROMPT; // 覆盖确认由对话框自带（QFileDialog 同）
    else
        options |= FOS_FILEMUSTEXIST;
    dialog->SetOptions(options);
    if (!title.isEmpty())
        dialog->SetTitle(reinterpret_cast<PCWSTR>(title.utf16()));

    QVector<QPair<QString, QString>> storage;
    const QVector<COMDLG_FILTERSPEC> specs = parseFilter(filter, storage);
    dialog->SetFileTypes(UINT(specs.size()), specs.constData());

    if (save) {
        // 保存对话框：dir 中的文件名作为默认名（调用方传 "xxx.json"/"名称.yaml"）
        const QFileInfo info(dir);
        if (!info.isDir() && !info.fileName().isEmpty())
            dialog->SetFileName(reinterpret_cast<PCWSTR>(info.fileName().utf16()));
        setStartDirectory(dialog.Get(), dir);
    } else {
        setStartDirectory(dialog.Get(), dir);
    }

    hr = dialog->Show(ownerHwnd());
    if (FAILED(hr)) {
        if (hr != HRESULT_FROM_WIN32(ERROR_CANCELLED))
            cwn::Log::warn(QStringLiteral("NativeFileDialog: Show failed 0x%1")
                               .arg(uint(hr), 8, 16, QLatin1Char('0')));
        return {};
    }

    ComPtr<IShellItem> result;
    if (FAILED(dialog->GetResult(&result)))
        return {};
    PWSTR rawPath = nullptr;
    if (FAILED(result->GetDisplayName(SIGDN_FILESYSPATH, &rawPath)))
        return {};
    const QString path = QString::fromWCharArray(rawPath);
    CoTaskMemFree(rawPath);
    return path;
}

} // namespace

namespace NativeFileDialog {

QString getOpenFileName(const QString &title, const QString &dir, const QString &filter)
{
    return runDialog(false, title, dir, filter);
}

QString getSaveFileName(const QString &title, const QString &dir, const QString &filter)
{
    return runDialog(true, title, dir, filter);
}

} // namespace NativeFileDialog
