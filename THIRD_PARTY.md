# 第三方组件与许可

本项目（Class Widgets Next）以 **Apache-2.0** 发布。仓库中包含以下第三方代码/资产：

## 1. Class Widgets 2（上游项目）

- 来源：https://github.com/RinLit-233-shiroko/Class-Widgets-2
- 许可：**MIT License**（Copyright (c) 2026 RinLit）
- 使用范围：`app/src/qml/` 全部 QML 文件、`app/src/themes/`、`app/assets/` 目录的资产
  （图标、音频、翻译）均原样复制自上游仓库（HEAD `1e66f09`）。
- 原始 MIT 许可文本见上游仓库 LICENSE；此处保留其版权声明。

## 2. RinUI（QML 控件库）

- 来源：https://github.com/RinLit-233-shiroko/RinUI （经 Class Widgets 2 v2.0.1.0
  Windows 发行包内附带的 `RinUI/` 目录提取）
- 许可：**MIT License**（Copyright (c) 2026 RinLit，发行包内 LICENSE 文件）
- 使用范围：`app/RinUI/` 目录（101 个 QML + 2 个 JS + FluentSystemIcons 字体 + 2 个 .qm）。
  其中 `components/Navigation/NavigationSubItem.qml` 已被上游 Class Widgets 2 的
  同名覆盖补丁替换（作者本人对该组件的修改，意图保持一致，详见 QML_MODIFICATIONS.md）。

## 3. Qt 6

- 许可：LGPLv3（动态链接，随包提供 Qt 库并允许用户替换）
- 本项目采用动态链接 Qt 6.10（Windows），符合 LGPL 对「可替换库文件」的要求。

---

### 与 Apache-2.0 的兼容性说明

MIT 许可的第三方代码与 Apache-2.0 兼容（MIT 条款更宽松）。按仓库所有者的决定，
本仓库整体以 Apache-2.0 归档；上述第三方文件继续按其原始 MIT 许可受保护，
其版权与许可声明随文件保留。
