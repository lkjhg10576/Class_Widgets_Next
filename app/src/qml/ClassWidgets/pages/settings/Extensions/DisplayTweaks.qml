import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RinUI

// 显示与小组件增强扩展配置页（four-plugins A3/B，P1 More_settings 去补丁化重实现）。
// 配置键 `extensions.display_tweaks.*`（15 键，见 ConfigStore 默认树；由
// `plugins.configs.com.kryon.more_settings` 一次性迁移，见 ExtensionManager）。
// 页结构照同目录 Weather.qml / SchedulePeek.qml：FluentPage + SettingCard 组；
// 健康自检黄条：DisplayTweaksService.healthCheck 失败时提示（只读黄条，不阻断）。
FluentPage {
    id: root

    horizontalPadding: 0
    wrapperWidth: Math.min(width - 42 * 2, 1000)

    title: qsTr("显示与小组件增强")

    readonly property var tweaks: (Configs.data.extensions && Configs.data.extensions.display_tweaks)
        ? Configs.data.extensions.display_tweaks : {}

    function boolOf(key, fallback) {
        const v = root.tweaks[key]
        return v === undefined ? fallback : !!v
    }

    // 排除科目列表（JSON 解析一次，加号上限禁用与文本框初值共用）
    readonly property var excludedList: {
        try {
            const arr = JSON.parse(root.tweaks.hide_excluded_subjects || "[]")
            return Array.isArray(arr) ? arr : []
        } catch (e) { return [] }
    }

    // 健康自检不再由本页发起（质检修正：原 healthCheck(true, true) 硬编码恒过、
    // 黄条分支不可达）——WidgetsWindow::onQmlReady 在主窗口 QML 就绪后以真实
    // findChild 结果判定，本页只绑定 DisplayTweaks.healthy 展示黄条。

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 4

        Text {
            typography: Typography.BodyStrong
            text: qsTr("显示与小组件增强")
        }

        // 健康黄条（补丁检测语义转健康自检：失败只提示，不要求卸载）
        Rectangle {
            Layout.fillWidth: true
            visible: DisplayTweaks && !DisplayTweaks.healthy
            color: Qt.rgba(1, 0.85, 0.4, 0.18)
            border.color: Qt.rgba(1, 0.7, 0.1, 0.6)
            border.width: 1
            radius: 8
            implicitHeight: healthText.implicitHeight + 20
            Text {
                id: healthText
                anchors.fill: parent
                anchors.margins: 10
                wrapMode: Text.Wrap
                color: Theme.currentTheme.colors.textColor
                text: (DisplayTweaks ? DisplayTweaks.healthMessage : "") || qsTr("显示增强自检未通过，功能可能部分失效")
            }
        }

        Item { Layout.fillWidth: true; Layout.preferredHeight: 4 }

        SettingCard {
            Layout.fillWidth: true
            icon.name: "ic_fluent_movies_and_tv_20_regular"
            title: qsTr("组件动画")
            description: qsTr("时间与倒数日数字滚动动画总开关")

            ColumnLayout {
                spacing: 6
                RowLayout {
                    spacing: 12
                    CheckBox {
                        text: qsTr("时间动画")
                        checked: root.boolOf("time_animation", true)
                        enabled: !Configs.isKeyLocked("extensions.display_tweaks.time_animation")
                        onToggled: Configs.set("extensions.display_tweaks.time_animation", checked)
                    }
                    CheckBox {
                        text: qsTr("倒数日动画")
                        checked: root.boolOf("countdown_animation", true)
                        enabled: !Configs.isKeyLocked("extensions.display_tweaks.countdown_animation")
                        onToggled: Configs.set("extensions.display_tweaks.countdown_animation", checked)
                    }
                }
            }
        }

        SettingCard {
            Layout.fillWidth: true
            icon.name: "ic_fluent_clock_20_regular"
            title: qsTr("时间显示")
            description: qsTr("秒、日期各分量与星期显隐")

            Flow {
                spacing: 10
                CheckBox {
                    text: qsTr("秒")
                    checked: root.boolOf("time_show_seconds", true)
                    enabled: !Configs.isKeyLocked("extensions.display_tweaks.time_show_seconds")
                    onToggled: Configs.set("extensions.display_tweaks.time_show_seconds", checked)
                }
                CheckBox {
                    text: qsTr("日期")
                    checked: root.boolOf("time_show_date", true)
                    enabled: !Configs.isKeyLocked("extensions.display_tweaks.time_show_date")
                    onToggled: Configs.set("extensions.display_tweaks.time_show_date", checked)
                }
                CheckBox {
                    text: qsTr("年")
                    checked: root.boolOf("time_show_year", true)
                    enabled: !Configs.isKeyLocked("extensions.display_tweaks.time_show_year")
                    onToggled: Configs.set("extensions.display_tweaks.time_show_year", checked)
                }
                CheckBox {
                    text: qsTr("月")
                    checked: root.boolOf("time_show_month", true)
                    enabled: !Configs.isKeyLocked("extensions.display_tweaks.time_show_month")
                    onToggled: Configs.set("extensions.display_tweaks.time_show_month", checked)
                }
                CheckBox {
                    text: qsTr("日")
                    checked: root.boolOf("time_show_day", true)
                    enabled: !Configs.isKeyLocked("extensions.display_tweaks.time_show_day")
                    onToggled: Configs.set("extensions.display_tweaks.time_show_day", checked)
                }
                CheckBox {
                    text: qsTr("星期")
                    checked: root.boolOf("time_show_weekday", true)
                    enabled: !Configs.isKeyLocked("extensions.display_tweaks.time_show_weekday")
                    onToggled: Configs.set("extensions.display_tweaks.time_show_weekday", checked)
                }
            }
        }

        SettingCard {
            Layout.fillWidth: true
            icon.name: "ic_fluent_text_position_square_20_regular"
            title: qsTr("标题布局")
            description: qsTr("日期与星期并排或交替显示")

            RowLayout {
                spacing: 8
                ComboBox {
                    Layout.preferredWidth: 180
                    model: ListModel {
                        ListElement { text: qsTr("并排显示"); value: "side_by_side" }
                        ListElement { text: qsTr("交替显示"); value: "alternate" }
                    }
                    textRole: "text"
                    valueRole: "value"
                    enabled: !Configs.isKeyLocked("extensions.display_tweaks.time_title_mode")
                    onCurrentValueChanged: if (focus) Configs.set("extensions.display_tweaks.time_title_mode", currentValue)
                    Component.onCompleted: currentIndex = indexOfValue(
                        root.tweaks.time_title_mode || "side_by_side")
                }
                SpinBox {
                    from: 500
                    to: 30000
                    stepSize: 500
                    enabled: !Configs.isKeyLocked("extensions.display_tweaks.time_alternate_interval")
                    textFromValue: (v) => (v / 1000) + qsTr(" 秒")
                    valueFromText: (t) => (parseFloat(t) || 3) * 1000
                    Component.onCompleted: value = root.tweaks.time_alternate_interval || 3000
                    onValueModified: Configs.set("extensions.display_tweaks.time_alternate_interval", value)
                }
                CheckBox {
                    text: qsTr("交替淡入淡出")
                    checked: root.boolOf("time_alternate_animation", false)
                    enabled: !Configs.isKeyLocked("extensions.display_tweaks.time_alternate_animation")
                    onToggled: Configs.set("extensions.display_tweaks.time_alternate_animation", checked)
                }
            }
        }

        SettingCard {
            Layout.fillWidth: true
            icon.name: "ic_fluent_resize_20_regular"
            title: qsTr("几何")
            description: qsTr("顶部距离（-1 跟随默认）与隐藏保留深度")

            RowLayout {
                spacing: 8
                Text { text: qsTr("顶部距离"); color: Theme.currentTheme.colors.textSecondaryColor }
                SpinBox {
                    from: -1
                    to: 500
                    Component.onCompleted: value = (root.tweaks.display_height === undefined) ? -1 : root.tweaks.display_height
                    enabled: !Configs.isKeyLocked("extensions.display_tweaks.display_height")
                    onValueModified: Configs.set("extensions.display_tweaks.display_height", value)
                }
                Text { text: qsTr("隐藏深度"); color: Theme.currentTheme.colors.textSecondaryColor }
                SpinBox {
                    from: -1
                    to: 200
                    // -1 = 跟随平台默认（质检修正：原 [0,200] 钳位封死哨兵，
                    // 平台默认分支不可达）
                    textFromValue: (v) => v < 0 ? qsTr("自动") : String(v)
                    valueFromText: (t) => { const n = parseInt(t); return isNaN(n) ? -1 : n }
                    Component.onCompleted: value = root.tweaks.hide_depth === undefined ? -1 : root.tweaks.hide_depth
                    enabled: !Configs.isKeyLocked("extensions.display_tweaks.hide_depth")
                    onValueModified: Configs.set("extensions.display_tweaks.hide_depth", value)
                }
            }
        }

        SettingCard {
            Layout.fillWidth: true
            icon.name: "ic_fluent_eye_off_20_regular"
            title: qsTr("特定课程不隐藏")
            description: qsTr("命中排除科目时纠正隐藏态（最多 20 门，逗号分隔）")

            ColumnLayout {
                spacing: 6
                CheckBox {
                    text: qsTr("启用")
                    checked: root.boolOf("hide_excluded_enabled", false)
                    enabled: !Configs.isKeyLocked("extensions.display_tweaks.hide_excluded_enabled")
                    onToggled: Configs.set("extensions.display_tweaks.hide_excluded_enabled", checked)
                }
                RowLayout {
                    spacing: 6
                    TextField {
                        id: excludedField
                        Layout.preferredWidth: 320
                        placeholderText: qsTr("如：自习,体育（≤20，加号达上限禁用）")
                        enabled: !Configs.isKeyLocked("extensions.display_tweaks.hide_excluded_subjects")
                                        && root.boolOf("hide_excluded_enabled", false)
                        Component.onCompleted: {
                            try {
                                const arr = JSON.parse(root.tweaks.hide_excluded_subjects || "[]")
                                text = Array.isArray(arr) ? arr.join(",") : ""
                            } catch (e) { text = "" }
                        }
                        onEditingFinished: {
                            // 全半角逗号都作分隔（质检修正：原仅 split(",")，
                            // 中文输入"自习，体育"整串成为一个永不命中的科目名）
                            const parts = text.split(/[,，]/).map((s) => s.trim())
                                .filter((s) => s.length > 0).slice(0, 20)
                            Configs.set("extensions.display_tweaks.hide_excluded_subjects", JSON.stringify(parts))
                        }
                    }
                    Button {
                        text: qsTr("添加当前课")
                        // 达 20 门上限禁用加号（质检修正：原实现仅 onClicked 内
                        // 静默 no-op，不满足 §7"上限禁用加号"验收）
                        enabled: excludedField.enabled && root.excludedList.length < 20
                        onClicked: {
                            const rt = AppCentral.scheduleRuntime
                            const cur = rt.currentEntry
                            // 科目名回退（与 WidgetsContainer.correctHideForExcluded
                            // 同口径）：title 为空时经 subjectId 查 subjects 全名
                            let name = cur ? (cur.title || "") : ""
                            if (!name && cur && cur.subjectId) {
                                const subjects = rt.subjects || []
                                for (let i = 0; i < subjects.length; i++) {
                                    if (subjects[i].id === cur.subjectId) {
                                        name = subjects[i].name || ""
                                        break
                                    }
                                }
                            }
                            let arr = root.excludedList.slice()
                            const candidate = name || ""
                            if (candidate && arr.length < 20 && arr.indexOf(candidate) < 0) {
                                arr.push(candidate)
                                Configs.set("extensions.display_tweaks.hide_excluded_subjects", JSON.stringify(arr))
                                excludedField.text = arr.join(",")
                            }
                        }
                    }
                }
            }
        }

        Item { Layout.fillWidth: true; Layout.preferredHeight: 8 }
    }
}
