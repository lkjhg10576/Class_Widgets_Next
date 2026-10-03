import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RinUI

// 课表速览扩展配置页（阶段 D 填充）：显示模式（extensions.schedule_peek.mode）
// 与分组间隔阈值（extensions.schedule_peek.split_gap_minutes）。
// 三件套范式照 General/Widgets.qml:98-124（滑杆）与 General/Interactions.qml:128-141
// （ComboBox）：enabled 受 isKeyLocked 约束 / 用户操作回写 Configs.set /
// Component.onCompleted 从 Configs.data 初始化；速览条本体（Components/SchedulePeekBar.qml）
// 读同一组键，改动即时生效无需重启。
FluentPage {
    id: root

    horizontalPadding: 0
    wrapperWidth: Math.min(width - 42 * 2, 1000)

    title: qsTr("课表速览")

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 4

        Text {
            typography: Typography.BodyStrong
            text: qsTr("课表速览")
        }

        SettingCard {
            Layout.fillWidth: true
            icon.name: "ic_fluent_eye_20_regular"
            title: qsTr("显示模式")
            description: qsTr("自动弹出：下课弹出、下次上课收起；常驻显示：当天有课即显示")

            // ConfigStore kScalarSpecs 白名单 auto|always（ConfigStore.cpp:61），
            // 非法值回退 auto 的处理在 SchedulePeekBar.qml 读取侧
            ComboBox {
                id: modeSelector
                Layout.preferredWidth: 180
                model: ListModel {
                    ListElement { text: qsTr("自动弹出"); value: "auto" }
                    ListElement { text: qsTr("常驻显示"); value: "always" }
                }
                textRole: "text"
                valueRole: "value"
                enabled: !Configs.isKeyLocked("extensions.schedule_peek.mode")
                // focus 门槛照 Interactions.qml:139：初始化 currentIndex 不得回写
                onCurrentValueChanged: if (focus) Configs.set("extensions.schedule_peek.mode", currentValue)
                Component.onCompleted: currentIndex = indexOfValue(
                    Configs.data.extensions.schedule_peek.mode || "auto")
            }
        }

        SettingCard {
            Layout.fillWidth: true
            icon.name: "ic_fluent_calendar_ltr_20_regular"
            title: qsTr("展示形态")
            description: qsTr("缩写格：单行首字格；全量条：全名 + 剩余倒计时 + 横向滚动")

            ComboBox {
                Layout.preferredWidth: 180
                model: ListModel {
                    ListElement { text: qsTr("缩写格"); value: "peek" }
                    ListElement { text: qsTr("全量条"); value: "full" }
                }
                textRole: "text"
                valueRole: "value"
                enabled: !Configs.isKeyLocked("extensions.schedule_peek.display_mode")
                onCurrentValueChanged: if (focus) Configs.set("extensions.schedule_peek.display_mode", currentValue)
                Component.onCompleted: currentIndex = indexOfValue(
                    (Configs.data.extensions.schedule_peek.display_mode) || "peek")
            }
        }

        SettingCard {
            Layout.fillWidth: true
            icon.name: "ic_fluent_slide_text_20_regular"
            title: qsTr("分组间隔阈值")
            description: qsTr("相邻两节课间隔达到该分钟数时，速览条中插入分组竖线；普通课间不插")

            Slider {
                id: gapSlider
                Layout.preferredWidth: 200
                from: 5
                to: 60
                stepSize: 5
                snapMode: Slider.SnapAlways
                tickmarks: true
                tickFrequency: 15
                enabled: !Configs.isKeyLocked("extensions.schedule_peek.split_gap_minutes")
                toolTip.text: qsTr("%1 分钟").arg(Math.round(value))
                // pressed 期间即写回（照 Widgets.qml:141 opacity 滑杆先例），
                // 落盘为整数分钟，与 ConfigStore ScalarKind::Int 规格一致
                onValueChanged: if (pressed)
                                    Configs.set("extensions.schedule_peek.split_gap_minutes", Math.round(value))
                Component.onCompleted: value = Configs.data.extensions.schedule_peek.split_gap_minutes || 15
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 8
        }
    }
}
