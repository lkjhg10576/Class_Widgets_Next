#pragma once

class QString;

// 对应上游 src/core/utils/auto_startup.py —— Windows 注册表 Run 键开机自启。
//
// 上游以 winreg 直写 HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run
// （auto_startup.py:28-34），值为字符串（REG_SZ）；本移植用 QSettings 原生格式
// 读写同一键（QSettings::NativeFormat 传注册表路径即直连该键），语义逐条对齐：
//   supported()          → autostart_supported()   （auto_startup.py:16-18）
//   enable()             → enable_autostart()      （auto_startup.py:21-38）
//   disable()            → disable_autostart()     （auto_startup.py:41-60）
//   isEnabled()          → is_autostart_enabled()  （auto_startup.py:63-83）
//   setEnabled(enabled)  → backend.py:257-261 的 enable/disable 分发
//
// 值名 = src/__init__.py:6 __app_name__ = "ClassWidgets_2"（auto_startup.py:8），
// 值 = 当前可执行文件绝对路径（auto_startup.py:9 打包形态取 sys.executable 的
// C++ 等价物 QCoreApplication::applicationFilePath()）。
// isEnabled 与上游一致做完整字符串比较（auto_startup.py:76），路径或安装位置
// 变化后视为未启用，由设置页重新写入。
namespace AutoStartup {

// auto_startup.py:16-18：本移植目标平台为 Windows（CMakeLists M1 注释）
bool supported();

// auto_startup.py:21-38：写入 Run 键；失败返回 false
bool enable();

// auto_startup.py:41-60：删除 Run 键值；键不存在视为成功（FileNotFoundError pass）
bool disable();

// auto_startup.py:63-83：Run 键值与当前可执行路径一致才视为已启用
bool isEnabled();

// backend.py:256-262 setAutostart 的内部分发
inline bool setEnabled(bool enabled)
{
    return enabled ? enable() : disable();
}

} // namespace AutoStartup
