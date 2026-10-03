import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RinUI

// 当日作业扩展配置页（扩展 classwidgets.ext.homework，F4）：
// 总开关走 Extensions.setEnabled；锁定/延迟/自动展示/通知/保留时长写
// extensions.homework.*；全部受 isKeyLocked 约束，并订阅 Configs.dataChanged
// 刷新控件（配置可被插件 API 锁定/改写）。作业管理入口 = 打开右侧作业浮窗。
// 三件套范式照 SchedulePeek.qml（enabled 受锁约束 / 用户操作回写 / 初始化读取）。
FluentPage {
    id: root

    horizontalPadding: 0
    wrapperWidth: Math.min(width - 42 * 2, 1000)

    title: qsTr("当日作业")

    readonly property var hwCfg: {
        const ext = Configs.data.extensions
        return (ext && ext.homework) ? ext.homework : {}
    }
    // isEnabled() 是 Q_INVOKABLE，绑定内显式读 Extensions.extensions 建立依赖。
    // 「打开作业浮窗」入口必须受总开关约束，否则禁用扩展后仍可从本页打开并
    // 使用完整编辑功能（上课/预备时 HomeworkTrigger 会自动收起它，F1 定稿）
    readonly property bool extEnabled: {
        Extensions.extensions
        return Extensions.isEnabled("classwidgets.ext.homework")
    }

    // 从配置重读全部控件状态（初始化 + dataChanged 刷新）。注意：
    // 1) 程序化赋值 checked 同样会触发 onToggled——不产生回环靠的是
    //    ConfigStore::set 对「值未变化」的短路，修改 set 语义时需连带审视本页；
    // 2) `enabled: !isKeyLocked(...)` 是 Q_INVOKABLE 调用，无通知依赖，插件
    //    锁键不会自动触发绑定重算，故 enabled 一并在这里显式刷新
    function refresh() {
        enableSwitch.enabled = !Configs.isKeyLocked("extensions.enabled")
        lockedSwitch.enabled = !Configs.isKeyLocked("extensions.homework.locked")
        delaySlider.enabled = !Configs.isKeyLocked("extensions.homework.delay_minutes")
        autoShowSwitch.enabled = !Configs.isKeyLocked("extensions.homework.auto_show")
        notifySwitch.enabled = !Configs.isKeyLocked("extensions.homework.notify_enabled")
        retentionBox.enabled = !Configs.isKeyLocked("extensions.homework.retention_days")
        enableSwitch.checked = Extensions.isEnabled("classwidgets.ext.homework")
        lockedSwitch.checked = root.hwCfg.locked !== false
        delaySlider.value = root.hwCfg.delay_minutes || 0
        autoShowSwitch.checked = root.hwCfg.auto_show !== false
        notifySwitch.checked = root.hwCfg.notify_enabled !== false
        const days = root.hwCfg.retention_days || 7
        retentionBox.currentIndex = retentionBox.indexOfValue(String(days))
        if (retentionBox.currentIndex < 0)
            retentionBox.currentIndex = retentionBox.indexOfValue("7")
    }

    Component.onCompleted: refresh()

    Connections {
        target: Configs
        function onDataChanged() { root.refresh() }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 4

        Text {
            typography: Typography.BodyStrong
            text: qsTr("当日作业")
        }

        SettingCard {
            Layout.fillWidth: true
            icon.name: "ic_fluent_clipboard_task_20_regular"
            title: qsTr("启用当日作业")
            description: qsTr("下课时弹出右侧作业浮窗并提醒课代表填写；数据按天保存在本地")

            Switch {
                id: enableSwitch
                onToggled: Extensions.setEnabled("classwidgets.ext.homework", checked)
            }
        }

        SettingCard {
            Layout.fillWidth: true
            icon.name: "ic_fluent_lock_closed_20_regular"
            title: qsTr("锁定位置与大小")
            description: qsTr("锁定后浮窗不可拖动、不显示缩放手柄；解锁后可自由拖动与调整大小")

            Switch {
                id: lockedSwitch
                onToggled: Configs.set("extensions.homework.locked", checked)
            }
        }

        SettingCard {
            Layout.fillWidth: true
            icon.name: "ic_fluent_timer_20_regular"
            title: qsTr("拖堂延迟")
            description: qsTr("下课铃后延迟该分钟数再弹出；期间切回上课或预备则本次不弹")

            Slider {
                id: delaySlider
                Layout.preferredWidth: 200
                from: 0
                to: 10
                stepSize: 1
                snapMode: Slider.SnapAlways
                tickmarks: true
                tickFrequency: 1
                toolTip.text: qsTr("%1 分钟").arg(Math.round(value))
                // pressed 期间即写回（照 SchedulePeek split_gap_minutes 滑杆先例），
                // 落盘为整数分钟，与 ConfigStore ScalarKind::Int 规格一致
                onValueChanged: if (pressed)
                                    Configs.set("extensions.homework.delay_minutes",
                                                Math.round(value))
            }
        }

        SettingCard {
            Layout.fillWidth: true
            icon.name: "ic_fluent_arrow_minimize_20_regular"
            title: qsTr("自动展示")
            description: qsTr("下课时自动弹出作业浮窗；关闭后仅能从此页或浮窗内手动打开")

            Switch {
                id: autoShowSwitch
                onToggled: Configs.set("extensions.homework.auto_show", checked)
            }
        }

        SettingCard {
            Layout.fillWidth: true
            icon.name: "ic_fluent_alert_20_regular"
            title: qsTr("灵动通知")
            description: qsTr("下课时播报“作业布置”通知；标记为不需要布置作业的科目不提醒")

            Switch {
                id: notifySwitch
                onToggled: Configs.set("extensions.homework.notify_enabled", checked)
            }
        }

        SettingCard {
            Layout.fillWidth: true
            icon.name: "ic_fluent_calendar_clock_20_regular"
            title: qsTr("保留时长")
            description: qsTr("作业文件保留最近 N 天，超期自动删除")

            ComboBox {
                id: retentionBox
                Layout.preferredWidth: 180
                model: ListModel {
                    ListElement { text: qsTr("1 天"); value: "1" }
                    ListElement { text: qsTr("3 天"); value: "3" }
                    ListElement { text: qsTr("7 天"); value: "7" }
                }
                textRole: "text"
                valueRole: "value"
                onActivated: Configs.set("extensions.homework.retention_days",
                                         Number(currentValue))
            }
        }

        SettingCard {
            Layout.fillWidth: true
            icon.name: "ic_fluent_settings_20_regular"
            title: qsTr("作业管理")
            description: qsTr("打开右侧作业浮窗，添加、编辑或删除当日作业")

            Button {
                text: qsTr("打开作业浮窗")
                icon.name: "ic_fluent_open_20_regular"
                enabled: root.extEnabled
                onClicked: if (root.extEnabled) WindowManager.openHomeworkFloat()
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 8
        }
    }
}
