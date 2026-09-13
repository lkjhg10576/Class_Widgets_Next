#pragma once

#include <QString>

// B4（内存优化）：win32 原生文件对话框（IFileOpenDialog/IFileSaveDialog），
// 替代 QFileDialog 静态方法，使进程可以只链接 Qt6::Gui（去掉 Qt6::Widgets）。
// 接口语义对齐 QFileDialog::getOpenFileName / getSaveFileName：
//   - 用户取消或失败返回空 QString；
//   - filter 采用 QFileDialog 格式："描述 (*.ext1 *.ext2);;描述2 (*.ext)"；
//   - 打开对话框：dir 为初始目录；保存对话框：dir 可含默认文件名。
// 仅在 GUI 线程调用（模态，内部自泵消息循环）。仅 Windows 有效。
namespace NativeFileDialog {

QString getOpenFileName(const QString &title, const QString &dir = {},
                        const QString &filter = {});
QString getSaveFileName(const QString &title, const QString &dir = {},
                        const QString &filter = {});

} // namespace NativeFileDialog
