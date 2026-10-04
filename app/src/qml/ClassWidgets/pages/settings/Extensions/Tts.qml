import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RinUI

// 语音播报扩展配置页（扩展 classwidgets.ext.tts，tts-extension 计划全量实施）。
// 配置键 extensions.tts.{engine,voice,volume}（标量）与 templates /
// provider_enabled（整表，经 TtsService 读写）；总开关走 Extensions.setEnabled。
// 服务对象必须走 AppCentral.tts，不能裸写 Tts.*：本页文件名 "Tts" 经同目录
// 隐式导入注册为页面作用域 QML 类型，优先级高于引擎 rootContext 上下文属性
// ——裸名解析到类型包装器，调用即抛 TypeError（同 RollCall.qml 教训）。
// 三件套范式照 Homework.qml（enabled 受锁约束 / 用户操作回写 / 初始化读取）。
// 唯一不落盘的页内 UI 态是语音语言筛选 selectedLocale（见 localeOptions 注释）。
FluentPage {
    id: root

    horizontalPadding: 0
    wrapperWidth: Math.min(width - 42 * 2, 1000)

    title: qsTr("语音播报")

    readonly property var ttsCfg: {
        const ext = Configs.data.extensions
        return (ext && ext.tts) ? ext.tts : {}
    }
    // isEnabled() 是 Q_INVOKABLE，绑定内显式读 Extensions.extensions 建立依赖
    readonly property bool extEnabled: {
        Extensions.extensions
        return Extensions.isEnabled("classwidgets.ext.tts")
    }
    readonly property var engineOptions: AppCentral.tts ? AppCentral.tts.availableEngines : ["auto"]
    // 语言筛选（localeOptions）：Qt 无独立语言列表可枚举，只能从 voiceList 的
    // locale 去重派生，首项「全部语言」= 不过滤。它只是本地 UI 态、不落盘，
    // 因此无需回写 TtsService（配置侧只有 engine/voice 两个键）。
    property string selectedLocale: ""
    readonly property var localeOptions: {
        const opts = [{ label: qsTr("全部语言"), value: "" }]
        const list = AppCentral.tts ? AppCentral.tts.voiceList : []
        const seen = {}
        for (let i = 0; i < list.length; i++) {
            const locale = list[i].locale || ""
            if (locale && !seen[locale]) {
                seen[locale] = true
                opts.push({ label: locale, value: locale })
            }
        }
        return opts
    }
    readonly property var voiceOptions: {
        const list = AppCentral.tts ? AppCentral.tts.voiceList : []
        // 首项文案按列表空/非空区分：空列表=当前引擎枚举不到语音（桩构建或
        // 未装语音包），非空=主动走引擎默认语音；两者都不是错误态
        const opts = [{
            label: list.length > 0 ? qsTr("引擎默认语音")
                                    : qsTr("默认语音（当前引擎不可用）"),
            value: ""
        }]
        for (let i = 0; i < list.length; i++) {
            if (root.selectedLocale && list[i].locale !== root.selectedLocale)
                continue
            opts.push({ label: list[i].name, value: list[i].id })
        }
        return opts
    }

    // 模板试听：把模板变量填入示例值（照 cw2-tts settings.qml 试听先例）。
    // 空模板 = 该类不播报（用户主动清空），直接返回空串，免得试听出一句空白；
    // {title} 用固定示例值而非模板键显示名——试听读的是通知原声，对齐 cw2-tts。
    function previewText(key) {
        if (!AppCentral.tts)
            return ""
        const tmpl = AppCentral.tts.getTemplate(key)
        if (!tmpl)
            return ""
        return tmpl
            .replace("{subject}", qsTr("语文"))
            .replace("{teacher}", qsTr("张老师"))
            .replace("{location}", qsTr("3号楼201"))
            .replace("{next_subject}", qsTr("数学"))
            .replace("{next_teacher}", qsTr("李老师"))
            .replace("{next_location}", qsTr("本班教室"))
            .replace("{title}", qsTr("通知标题"))
            .replace("{message}", qsTr("通知消息"))
    }

    function refresh() {
        if (!AppCentral.tts)
            return
        enableSwitch.enabled = !Configs.isKeyLocked("extensions.enabled")
        engineCombo.enabled = !Configs.isKeyLocked("extensions.tts.engine")
        voiceCombo.enabled = !Configs.isKeyLocked("extensions.tts.voice")
        refreshVoicesButton.enabled = voiceCombo.enabled
        volumeSlider.enabled = !Configs.isKeyLocked("extensions.tts.volume")
        // 测试框是页内临时输入（testSpeak 不落盘），无对应配置键——绑音量锁键
        // 只会让锁 volume 的用户连试读都不能用，故不设 enabled
        const tplLocked = Configs.isKeyLocked("extensions.tts.templates")
        classField.enabled = !tplLocked
        activityField.enabled = !tplLocked
        breakField.enabled = !tplLocked
        freeField.enabled = !tplLocked
        preparationField.enabled = !tplLocked

        enableSwitch.checked = Extensions.isEnabled("classwidgets.ext.tts")
        engineCombo.currentIndex = Math.max(0, root.engineOptions.indexOf(AppCentral.tts.engine))
        const voice = AppCentral.tts.voice || ""
        let vi = 0
        for (let i = 0; i < root.voiceOptions.length; i++) {
            if (root.voiceOptions[i].value === voice) {
                vi = i
                break
            }
        }
        voiceCombo.currentIndex = vi
        // 语言筛选是页内 UI 态，回填时定位到「全部语言」；程序化赋值同样会
        // 触发 onCurrentValueChanged，focus 门槛保证不产生回写
        let li = 0
        for (let i = 0; i < root.localeOptions.length; i++) {
            if (root.localeOptions[i].value === root.selectedLocale) {
                li = i
                break
            }
        }
        localeCombo.currentIndex = li
        volumeSlider.value = Math.round((AppCentral.tts.volume || 0) * 100)
        // 模板回填：用户正在编辑的行不覆盖（activeFocus 守卫），避免 dataChanged
        // （如音量滑杆拖动）打断输入
        if (!classField.activeFocus)
            classField.text = AppCentral.tts.getTemplate("class")
        if (!activityField.activeFocus)
            activityField.text = AppCentral.tts.getTemplate("activity")
        if (!breakField.activeFocus)
            breakField.text = AppCentral.tts.getTemplate("break")
        if (!freeField.activeFocus)
            freeField.text = AppCentral.tts.getTemplate("free")
        if (!preparationField.activeFocus)
            preparationField.text = AppCentral.tts.getTemplate("preparation")
        // 朗读范围列表：model 绑定 AppCentral.tts.providers（见 Repeater），
        // 这里只逐行 sync() —— providerEnabled() 是函数调用、无 NOTIFY，
        // 外部改写 configs.json 时开关不会自动重算，必须命令式同步
        for (let p = 0; p < providerRepeater.count; p++) {
            const item = providerRepeater.itemAt(p)
            if (item)
                item.sync()
        }
    }

    Component.onCompleted: {
        testField.text = qsTr("同学们好，现在开始上课。")
        refresh()
    }

    Connections {
        target: Configs
        function onDataChanged() { root.refresh() }
    }
    Connections {
        target: AppCentral.tts
        function onVoiceListChanged() { root.refresh() }
        function onEngineChanged() { root.refresh() }
        function onTemplatesChanged() { root.refresh() }
        function onProvidersChanged() { root.refresh() }
        // 朗读状态变化走信号而非「点按钮后置灰」：队列串行、自动转移播报与
        // 程序 stopSpeaking() 三条路径都会改它，绑定式 enabled 免得各漏一处
        function onSpeakingChanged() { root.refresh() }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 4

        Text {
            typography: Typography.BodyStrong
            text: qsTr("语音播报")
        }

        // 健康黄条（桩构建/无引擎时只读提示，不阻断设置页；范式照 DisplayTweaks.qml）
        Rectangle {
            Layout.fillWidth: true
            visible: AppCentral.tts && !AppCentral.tts.healthy
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
                text: (AppCentral.tts ? AppCentral.tts.healthMessage : "")
                      || qsTr("语音播报不可用")
            }
        }

        SettingCard {
            Layout.fillWidth: true
            icon.name: "ic_fluent_speaker_2_20_regular"
            title: qsTr("启用语音播报")
            description: qsTr("收到通知时按模板朗读；关闭后立即停声并断开通知订阅")

            Switch {
                id: enableSwitch
                onToggled: Extensions.setEnabled("classwidgets.ext.tts", checked)
            }
        }

        Text {
            typography: Typography.BodyStrong
            Layout.topMargin: 8
            text: qsTr("引擎设置")
        }

        SettingCard {
            Layout.fillWidth: true
            icon.name: "ic_fluent_bot_20_regular"
            title: qsTr("TTS 引擎")
            description: (AppCentral.tts && AppCentral.tts.activeEngine)
                ? qsTr("当前使用：%1").arg(AppCentral.tts.activeEngine)
                : qsTr("无可用引擎")

            ComboBox {
                id: engineCombo
                Layout.preferredWidth: 200
                model: root.engineOptions
                // focus 门槛照 SchedulePeek.qml：初始化 currentIndex 不得回写
                onCurrentValueChanged: if (focus && AppCentral.tts)
                                           AppCentral.tts.setEngine(currentValue)
                Component.onCompleted: currentIndex = Math.max(
                    0, root.engineOptions.indexOf(AppCentral.tts ? AppCentral.tts.engine : "auto"))
            }
        }

        SettingCard {
            Layout.fillWidth: true
            icon.name: "ic_fluent_person_voice_20_regular"
            title: qsTr("语音")
            description: (AppCentral.tts && AppCentral.tts.voice)
                ? AppCentral.tts.voice : qsTr("使用引擎默认语音")

            RowLayout {
                spacing: 6

                // 语言筛选置于语音下拉之前：Windows 上 winrt 一次可枚举数百个
                // 语音，不筛根本挑不出来；focus 门槛同 voiceCombo（初始化
                // currentIndex 不得回写），且它只改页内 selectedLocale 不落盘
                ComboBox {
                    id: localeCombo
                    Layout.preferredWidth: 140
                    model: root.localeOptions
                    textRole: "label"
                    valueRole: "value"
                    onCurrentValueChanged: if (focus)
                                               root.selectedLocale = currentValue
                }

                ComboBox {
                    id: voiceCombo
                    Layout.preferredWidth: 240
                    model: root.voiceOptions
                    textRole: "label"
                    valueRole: "value"
                    onCurrentValueChanged: if (focus && AppCentral.tts)
                                               AppCentral.tts.setVoice(currentValue)
                }

                Button {
                    id: refreshVoicesButton
                    icon.name: "ic_fluent_arrow_sync_20_regular"
                    text: qsTr("刷新")
                    onClicked: if (AppCentral.tts) AppCentral.tts.refreshVoices()
                }
            }
        }

        Text {
            typography: Typography.BodyStrong
            Layout.topMargin: 8
            text: qsTr("播放设置")
        }

        SettingCard {
            Layout.fillWidth: true
            icon.name: "ic_fluent_speaker_2_20_regular"
            title: qsTr("音量")
            description: qsTr("%1%").arg(Math.round(volumeSlider.value))

            Slider {
                id: volumeSlider
                Layout.preferredWidth: 200
                from: 0
                to: 100
                stepSize: 1
                snapMode: Slider.SnapAlways
                tickmarks: true
                tickFrequency: 10
                toolTip.text: qsTr("%1%").arg(Math.round(value))
                // pressed 期间即写回（照 Homework delaySlider 先例），落盘靠自动保存
                onValueChanged: if (pressed && AppCentral.tts)
                                    AppCentral.tts.setVolume(value / 100)
            }
        }

        SettingCard {
            Layout.fillWidth: true
            icon.name: "ic_fluent_play_20_regular"
            title: qsTr("测试朗读")
            description: qsTr("使用当前引擎、语音与音量朗读")

            RowLayout {
                spacing: 6

                TextField {
                    id: testField
                    Layout.preferredWidth: 280
                    placeholderText: qsTr("输入要朗读的文本")
                }

                Button {
                    text: qsTr("朗读")
                    icon.name: "ic_fluent_play_20_regular"
                    onClicked: if (AppCentral.tts) AppCentral.tts.testSpeak(testField.text)
                }

                Button {
                    icon.name: "ic_fluent_stop_20_regular"
                    text: qsTr("停止")
                    // 绑定 speaking 而非点按后置灰：模板试听 / 通知自动播报 /
                    // 队列里还有下一条时都该可停，空闲时点「停止」无意义
                    enabled: AppCentral.tts && AppCentral.tts.speaking
                    onClicked: if (AppCentral.tts) AppCentral.tts.stopSpeaking()
                }
            }
        }

        Text {
            typography: Typography.BodyStrong
            Layout.topMargin: 8
            text: qsTr("朗读模板")
        }

        Text {
            Layout.fillWidth: true
            typography: Typography.Caption
            color: Theme.currentTheme.colors.textSecondaryColor
            wrapMode: Text.WordWrap
            text: qsTr("可用变量：{title} 通知标题、{message} 通知正文、{subject} 科目、{teacher} 教师、{location} 地点、{next_subject} 下节科目、{next_teacher} 下节教师、{next_location} 下节地点。清空模板即该类不播报。")
        }

        SettingCard {
            Layout.fillWidth: true
            icon.name: "ic_fluent_book_20_regular"
            title: qsTr("上课")
            description: qsTr("课程开始通知朗读模板")

            RowLayout {
                spacing: 6

                TextField {
                    id: classField
                    Layout.preferredWidth: 300
                    placeholderText: AppCentral.tts ? AppCentral.tts.defaultTemplate("class") : ""
                    onEditingFinished: if (AppCentral.tts)
                                            AppCentral.tts.setTemplate("class", text)
                }

                Button {
                    text: qsTr("重置")
                    // 五处「重置」一律不调 refresh()：模板真的变了会发
                    // templatesChanged 触发刷新；已是默认值时 setTemplate 短路
                    // 不发信号，此时回填的本就是默认值，无需再刷一遍
                    onClicked: if (AppCentral.tts) AppCentral.tts.resetTemplate("class")
                }

                Button {
                    text: qsTr("试听")
                    icon.name: "ic_fluent_play_20_regular"
                    onClicked: if (AppCentral.tts) AppCentral.tts.testSpeak(root.previewText("class"))
                }
            }
        }

        SettingCard {
            Layout.fillWidth: true
            icon.name: "ic_fluent_people_20_regular"
            title: qsTr("活动开始")
            description: qsTr("活动开始通知朗读模板")

            RowLayout {
                spacing: 6

                TextField {
                    id: activityField
                    Layout.preferredWidth: 300
                    placeholderText: AppCentral.tts ? AppCentral.tts.defaultTemplate("activity") : ""
                    onEditingFinished: if (AppCentral.tts)
                                            AppCentral.tts.setTemplate("activity", text)
                }

                Button {
                    text: qsTr("重置")
                    onClicked: if (AppCentral.tts) AppCentral.tts.resetTemplate("activity")
                }

                Button {
                    text: qsTr("试听")
                    icon.name: "ic_fluent_play_20_regular"
                    onClicked: if (AppCentral.tts) AppCentral.tts.testSpeak(root.previewText("activity"))
                }
            }
        }

        SettingCard {
            Layout.fillWidth: true
            icon.name: "ic_fluent_drink_coffee_20_regular"
            title: qsTr("下课")
            description: qsTr("课间通知朗读模板")

            RowLayout {
                spacing: 6

                TextField {
                    id: breakField
                    Layout.preferredWidth: 300
                    placeholderText: AppCentral.tts ? AppCentral.tts.defaultTemplate("break") : ""
                    onEditingFinished: if (AppCentral.tts)
                                            AppCentral.tts.setTemplate("break", text)
                }

                Button {
                    text: qsTr("重置")
                    onClicked: if (AppCentral.tts) AppCentral.tts.resetTemplate("break")
                }

                Button {
                    text: qsTr("试听")
                    icon.name: "ic_fluent_play_20_regular"
                    onClicked: if (AppCentral.tts) AppCentral.tts.testSpeak(root.previewText("break"))
                }
            }
        }

        SettingCard {
            Layout.fillWidth: true
            icon.name: "ic_fluent_home_20_regular"
            title: qsTr("放学")
            description: qsTr("放学通知朗读模板")

            RowLayout {
                spacing: 6

                TextField {
                    id: freeField
                    Layout.preferredWidth: 300
                    placeholderText: AppCentral.tts ? AppCentral.tts.defaultTemplate("free") : ""
                    onEditingFinished: if (AppCentral.tts)
                                            AppCentral.tts.setTemplate("free", text)
                }

                Button {
                    text: qsTr("重置")
                    onClicked: if (AppCentral.tts) AppCentral.tts.resetTemplate("free")
                }

                Button {
                    text: qsTr("试听")
                    icon.name: "ic_fluent_play_20_regular"
                    onClicked: if (AppCentral.tts) AppCentral.tts.testSpeak(root.previewText("free"))
                }
            }
        }

        SettingCard {
            Layout.fillWidth: true
            icon.name: "ic_fluent_alert_20_regular"
            title: qsTr("预备铃")
            description: qsTr("预备铃通知朗读模板")

            RowLayout {
                spacing: 6

                TextField {
                    id: preparationField
                    Layout.preferredWidth: 300
                    placeholderText: AppCentral.tts ? AppCentral.tts.defaultTemplate("preparation") : ""
                    onEditingFinished: if (AppCentral.tts)
                                            AppCentral.tts.setTemplate("preparation", text)
                }

                Button {
                    text: qsTr("重置")
                    onClicked: if (AppCentral.tts) AppCentral.tts.resetTemplate("preparation")
                }

                Button {
                    text: qsTr("试听")
                    icon.name: "ic_fluent_play_20_regular"
                    onClicked: if (AppCentral.tts) AppCentral.tts.testSpeak(root.previewText("preparation"))
                }
            }
        }

        Text {
            typography: Typography.BodyStrong
            Layout.topMargin: 8
            text: qsTr("朗读范围")
        }

        Text {
            Layout.fillWidth: true
            typography: Typography.Caption
            color: Theme.currentTheme.colors.textSecondaryColor
            wrapMode: Text.WordWrap
            // 补「应用内通知保持开启」：朗读只挂在 NotificationService::notified
            // 上，源头不发的通知（如系统勿扰、或通知自身被关）这里开关再开也不播
            text: qsTr("按通知来源逐项开关朗读；关闭后该来源的通知不再播报（通知本身不受影响）。需该来源的应用内通知保持开启，否则不会播报")
        }

        Repeater {
            id: providerRepeater
            // model 绑定化：providers 是 C++ 侧带 NOTIFY 的属性（转发
            // NotificationService::notificationProvidersChanged）。原来在
            // refresh() 里命令式赋 model，会被拖动音量滑杆触发的 dataChanged
            // 带着整份列表重建一次 SettingCard——高频操作下的纯浪费
            model: AppCentral.tts ? AppCentral.tts.providers : []

            delegate: SettingCard {
                id: providerCard
                required property int index
                required property var modelData

                Layout.fillWidth: true
                icon.name: modelData.icon || "ic_fluent_megaphone_20_regular"
                title: modelData.name || modelData.id
                description: modelData.id || ""

                function sync() {
                    providerSwitch.enabled = !Configs.isKeyLocked("extensions.tts.provider_enabled")
                    if (AppCentral.tts)
                        providerSwitch.checked = AppCentral.tts.providerEnabled(modelData.id)
                }

                Component.onCompleted: sync()

                Switch {
                    id: providerSwitch
                    onToggled: if (AppCentral.tts)
                                    AppCentral.tts.setProviderEnabled(providerCard.modelData.id, checked)
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 8
        }
    }
}
