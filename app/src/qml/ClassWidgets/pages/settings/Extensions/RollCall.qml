import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import RinUI

// 随机点名扩展配置页（阶段 C2，extensions-feature-plan §6）：TXT 导入、名单
// 增删改与权重（-100%~+100%）、重复策略。数据契约：配置键
// extensions.roll_call.names（[{name, weight}]，重名/钳位校验在 RollCallService），
// avoid_repeat（single|session，ConfigStore kScalarSpecs 白名单）。
// 页结构照同目录 Weather.qml（FluentPage + 分组标题 + SettingCard 组）；
// ComboBox 三件套照同目录 SchedulePeek.qml:37-51（enabled 锁定检查 /
// onCurrentValueChanged+focus 门槛回写 / Component.onCompleted 初始化）。
FluentPage {
    id: root

    horizontalPadding: 0
    wrapperWidth: Math.min(width - 42 * 2, 1000)

    title: qsTr("随机点名")

    // 名单实时源：Configs.data 整树在 dataChanged 时重估 —— 服务写回
    // （addName/updateName/...）与导入合并都走同一条通路。注意任何配置键的
    // 写入（含悬浮窗拖按钮写 button_x/y）都会重建 Repeater 委托；本页编辑
    // 提交后本就期望刷新，拖拽同时开着本页属罕见并发，可接受
    readonly property var roster: (Configs.data.extensions
                                   && Configs.data.extensions.roll_call
                                   && Configs.data.extensions.roll_call.names) || []
    // 一次性操作反馈（导入结果/添加失败/删除），显示在"手动添加"卡片描述行
    property string statusText: ""

    function canEdit() {
        return !Configs.isKeyLocked("extensions.roll_call.names")
    }

    // 权重显示：+20% / 0% / -100%（正数带加号，与滑杆语义对齐）
    function weightLabel(value) {
        const v = Math.round(value)
        return (v > 0 ? "+" : "") + v + "%"
    }

    // 导入：文件解析走 C++（QML 无法读本地文件），服务纯解析无副作用；
    // 合并去重在此完成，再经 addName 逐个写回 —— 复用服务的 trim/重名校验
    // 与即时落盘（每名字一次 configs.json 写，文件几 KB、班级规模几十次
    // 毫秒级，换取单一写入口径，不做批量捷径）
    function importFromFile(url) {
        if (!RollCall)
            return
        const imported = RollCall.importNamesFromUrl(url)
        if (imported.length === 0) {
            root.statusText = qsTr("导入失败：文件为空、无法读取或不含有效名字")
            return
        }
        const existing = new Set(root.roster.map(e => e.name))
        let added = 0
        for (const item of imported) {
            if (existing.has(item.name))
                continue
            if (RollCall.addName(item.name)) {
                existing.add(item.name)
                added += 1
            }
        }
        root.statusText = qsTr("导入完成：新增 %1 人，跳过重名 %2 人")
            .arg(added).arg(imported.length - added)
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 4

        Text {
            typography: Typography.BodyStrong
            text: qsTr("随机点名")
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 4
        }

        // ── 导入 ──
        SettingCard {
            Layout.fillWidth: true

            icon.name: "ic_fluent_arrow_upload_20_regular"
            title: qsTr("名单")
            // 顶部统计（加分项）：有名单显示人数，空名单给引导
            description: root.roster.length > 0
                ? qsTr("共 %1 人").arg(root.roster.length)
                : qsTr("名单为空：从 TXT 导入（每行一个名字，UTF-8）或手动添加")

            RowLayout {
                spacing: 6

                Button {
                    text: qsTr("从 TXT 导入")
                    enabled: root.canEdit()
                    onClicked: importDialog.open()
                }
                Button {
                    text: qsTr("清空名单")
                    enabled: root.canEdit() && root.roster.length > 0
                    onClicked: {
                        if (RollCall) {
                            RollCall.clearNames()
                            root.statusText = qsTr("名单已清空")
                        }
                    }
                }
            }
        }

        // ── 手动添加 + 操作反馈 ──
        SettingCard {
            Layout.fillWidth: true

            icon.name: "ic_fluent_add_20_regular"
            title: qsTr("手动添加")
            description: root.statusText.length > 0
                ? root.statusText
                : qsTr("重名会被拒绝并提示")

            RowLayout {
                spacing: 6

                TextField {
                    id: addField
                    Layout.preferredWidth: 220
                    placeholderText: qsTr("输入姓名")
                    enabled: root.canEdit()
                    onAccepted: addButton.doAdd()
                }

                Button {
                    id: addButton
                    text: qsTr("添加")
                    enabled: root.canEdit()
                    function doAdd() {
                        if (!RollCall)
                            return
                        // addName 内含 trim 与重名拒绝：返回 false 即失败原因二选一
                        const name = addField.text.trim()
                        if (RollCall.addName(name)) {
                            addField.text = ""
                            root.statusText = qsTr("已添加「%1」").arg(name)
                        } else {
                            root.statusText = qsTr("添加失败：姓名为空或与现有名单重复")
                        }
                    }
                    onClicked: doAdd()
                }
            }
        }

        // ── 名单逐行编辑：名字文本框（回车/失焦提交）+ 权重滑杆（-100~100，
        // 步进 5，松手提交）+ 删除 ──
        Repeater {
            model: root.roster

            delegate: SettingCard {
                id: rowCard
                required property int index
                required property var modelData

                Layout.fillWidth: true
                icon.name: "ic_fluent_person_20_regular"
                title: modelData.name
                description: qsTr("权重 %1（-100% 永不抽中，+100% 概率翻倍）")
                    .arg(root.weightLabel(rowSlider.value))

                RowLayout {
                    spacing: 8

                    TextField {
                        id: nameField
                        Layout.preferredWidth: 140
                        text: rowCard.modelData.name
                        enabled: root.canEdit()
                        // 提交走服务 updateName：重名/空名拒绝后回读现值，
                        // 成功则 dataChanged 重建本行（编辑已结束，重建无感）
                        onEditingFinished: {
                            if (!RollCall)
                                return
                            if (!RollCall.updateName(rowCard.index, text,
                                                     Math.round(rowCard.modelData.weight))) {
                                root.statusText = qsTr("改名失败：姓名为空或与其他行重名")
                                text = (root.roster[rowCard.index] || {}).name || ""
                            }
                        }
                    }

                    Slider {
                        id: rowSlider
                        Layout.preferredWidth: 220
                        from: -100
                        to: 100
                        stepSize: 5
                        snapMode: Slider.SnapAlways
                        enabled: root.canEdit()
                        value: rowCard.modelData.weight
                        // toolTip 用组件默认可见性（pressed/hovered 时弹出），
                        // 内部 handle 节点不可引用，故不覆写 toolTip.visible
                        toolTip.text: root.weightLabel(value)
                        // 松手提交（与滑杆实时 toolTip 预览配对）：拖动期间
                        // 不写配置，避免每步触发 dataChanged 重建委托打断拖拽
                        onPressedChanged: {
                            if (pressed || !RollCall)
                                return
                            if (!RollCall.updateName(rowCard.index, nameField.text,
                                                     Math.round(rowSlider.value))) {
                                root.statusText = qsTr("权重提交失败：名字可能已被改名")
                                rowSlider.value = rowCard.modelData.weight
                            }
                        }
                    }

                    Text {
                        // 实时百分比标注（跟随滑杆预览，提交仍以松手为准）
                        Layout.preferredWidth: 52
                        text: root.weightLabel(rowSlider.value)
                        color: Theme.currentTheme.colors.textSecondaryColor
                        horizontalAlignment: Text.AlignRight
                    }

                    Button {
                        text: qsTr("删除")
                        enabled: root.canEdit()
                        onClicked: {
                            if (RollCall && RollCall.removeName(rowCard.index))
                                root.statusText = qsTr("已删除「%1」").arg(rowCard.modelData.name)
                        }
                    }
                }
            }
        }

        // ── 重复策略 ──
        SettingCard {
            Layout.fillWidth: true

            icon.name: "ic_fluent_arrow_sync_20_regular"
            title: qsTr("重复策略")
            description: qsTr("单次内不重复：每次点名从全体重抽；会话内不重复：本会话点过的人排除，关闭结果窗口后重置")

            ComboBox {
                id: modeSelector
                Layout.preferredWidth: 200
                model: ListModel {
                    ListElement { text: qsTr("单次内不重复"); value: "single" }
                    ListElement { text: qsTr("会话内不重复"); value: "session" }
                }
                textRole: "text"
                valueRole: "value"
                enabled: !Configs.isKeyLocked("extensions.roll_call.avoid_repeat")
                // focus 门槛照 SchedulePeek.qml:48：初始化 currentIndex 不得回写
                onCurrentValueChanged: if (focus)
                                           Configs.set("extensions.roll_call.avoid_repeat", currentValue)
                Component.onCompleted: currentIndex = indexOfValue(
                    Configs.data.extensions.roll_call.avoid_repeat || "single")
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 8
        }
    }

    // Qt6 标准 FileDialog：项目内无既有用例（grep 实测），按 QtQuick.Dialogs
    // 6.x 写法（默认 fileMode OpenFile，selectedFile 为 URL）；Qt 6.9 下
    // Windows 走原生对话框。声明为页面根的直接子级而非 Layout 子项 ——
    // 原生对话框不是可见布局项，放进 ColumnLayout 会被布局按 Item 处理
    FileDialog {
        id: importDialog
        title: qsTr("导入名单（TXT，每行一个名字）")
        nameFilters: [qsTr("纯文本文件 (*.txt)"), qsTr("所有文件 (*)")]
        onAccepted: root.importFromFile(importDialog.selectedFile)
    }
}
