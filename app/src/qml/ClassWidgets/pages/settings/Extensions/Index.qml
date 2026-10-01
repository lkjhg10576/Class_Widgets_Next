import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RinUI

// 「扩展功能」列表页（阶段 A 骨架）：渲染 ExtensionManager 的静态注册表，
// 开关写回 extensions.enabled。扩展与插件（Plugins.qml / plugins.*）严格分离：
// 这里是官方功能模块的开关与调参入口，不加载任何第三方代码。
FluentPage {
    id: root

    horizontalPadding: 0
    wrapperWidth: Math.min(width - 42 * 2, 1000)

    title: qsTr("Extensions")

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 4

        Text {
            typography: Typography.BodyStrong
            text: qsTr("Extensions")
        }

        // 数据源每项 {id, name, icon, description, enabled, hasSettings, settingsPage}；
        // Extensions.extensionsChanged 触发列表重建
        Repeater {
            model: Extensions.extensions

            delegate: SettingCard {
                Layout.fillWidth: true
                icon.name: modelData.icon
                title: modelData.name
                description: modelData.description

                Switch {
                    // 绑定模型值而非手动写 checked：开关切换后 extensionsChanged
                    // 重建数据源，checked 始终反映落盘的实际启用状态
                    enabled: !Configs.isKeyLocked("extensions.enabled")
                    checked: modelData.enabled
                    onToggled: Extensions.setEnabled(modelData.id, checked)
                }

                Button {
                    visible: modelData.hasSettings
                    text: qsTr("Settings")
                    // 跳页写法照 pages/settings/Home.qml:14-16（navigationView 为
                    // 设置窗口 FluentWindow 的导航作用域）
                    onClicked: navigationView.push(PathManager.qml(modelData.settingsPage))
                }
            }
        }
    }
}
