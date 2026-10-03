import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RinUI

// 当日作业编辑对话框（扩展 classwidgets.ext.homework，F5/F7）：
// 科目下拉（过滤 needsHomework !== false，含「不指定」）、正文必填、
// 优先级四选一（none/orange/blue/green）；确认后经 Homework 服务写当天文件。
// 双入口复用：浮窗底部「+」新建（itemId 空）与列表选中「编辑」（itemId 回填）。
// 入口统一走 openFor(itemId, subjectId, content, priority)。
Dialog {
    id: editor

    property string itemId: ""
    property string presetSubjectId: ""
    // 科目下拉数据快照：打开时刷新（避免编辑期间课表推送重建模型打断选择）
    property var subjectModel: []

    modal: true
    title: editor.itemId ? qsTr("编辑作业") : qsTr("添加作业")
    // 对话框寄宿于作业浮窗（可能被用户缩小），宽度跟随可用空间
    width: editor.parent ? Math.min(380, editor.parent.width - 24) : 380

    // 打开入口：id 为空即新建；其余参数回填现有值（空值安全）
    function openFor(id, subjectId, contentText, priorityValue) {
        editor.itemId = id || ""
        editor.presetSubjectId = subjectId || ""
        editor.refreshSubjects()
        // 重建模型后按 id 定位；科目已被删除/过滤则回退「不指定」（index 0）
        subjectBox.currentIndex = editor.indexOfSubject(editor.presetSubjectId)
        contentArea.text = contentText || ""
        priorityBox.currentIndex = priorityBox.indexOfValue(priorityValue || "none")
        if (priorityBox.currentIndex < 0)
            priorityBox.currentIndex = 0
        editor.open()
        contentArea.forceActiveFocus()
        // Ok 门槛唯一驱动是 contentArea.onTextChanged；回填文本与旧值相同时
        // 信号不触发，这里显式同步一次（新建入口必须以禁用态起步）
        editor.syncOkEnabled()
    }

    // 科目选项：首项「不指定」+ 需要布置作业的科目（needsHomework === false 过滤）
    function subjectOptions() {
        const out = [{ text: qsTr("不指定"), value: "" }]
        const runtime = AppCentral.scheduleRuntime
        const subjects = runtime ? (runtime.subjects || []) : []
        for (let i = 0; i < subjects.length; ++i) {
            const subject = subjects[i]
            if (subject.needsHomework === false)
                continue
            out.push({ text: subject.name || subject.id, value: subject.id })
        }
        return out
    }

    function refreshSubjects() {
        editor.subjectModel = editor.subjectOptions()
    }

    function indexOfSubject(id) {
        if (!id)
            return 0
        for (let i = 0; i < editor.subjectModel.length; ++i) {
            if (editor.subjectModel[i].value === id)
                return i
        }
        return 0
    }

    contentItem: ColumnLayout {
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Text {
                text: qsTr("科目")
                Layout.preferredWidth: 56
            }

            ComboBox {
                id: subjectBox
                Layout.fillWidth: true
                model: editor.subjectModel
                textRole: "text"
                valueRole: "value"
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Text {
                text: qsTr("内容")
                Layout.preferredWidth: 56
                Layout.alignment: Qt.AlignTop
            }

            TextArea {
                id: contentArea
                Layout.fillWidth: true
                Layout.preferredHeight: 88
                wrapMode: TextEdit.Wrap
                placeholderText: qsTr("如：练习册 P4~P6")
                onTextChanged: editor.syncOkEnabled()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Text {
                text: qsTr("优先级")
                Layout.preferredWidth: 56
            }

            ComboBox {
                id: priorityBox
                Layout.fillWidth: true
                // 优先级仅整行字体变色（none=默认色），取值与 HomeworkService 白名单一致
                model: ListModel {
                    ListElement { text: qsTr("无"); value: "none" }
                    ListElement { text: qsTr("橙色"); value: "orange" }
                    ListElement { text: qsTr("蓝色"); value: "blue" }
                    ListElement { text: qsTr("绿色"); value: "green" }
                }
                textRole: "text"
                valueRole: "value"
            }
        }
    }

    // 正文必填：OK 仅在非空白内容时可用（照 RescheduleDayDialog 的 okButton 门槛）
    function syncOkEnabled() {
        if (editor.footer && editor.footer.okButton)
            editor.footer.okButton.enabled = contentArea.text.trim().length > 0
    }

    footer: DialogButtonBox {
        standardButtons: DialogButtonBox.Ok | DialogButtonBox.Cancel
        property Button okButton: standardButton(DialogButtonBox.Ok)

        Component.onCompleted: okButton.enabled = false

        onAccepted: {
            const content = contentArea.text.trim()
            if (content.length === 0 || !editor.footer)
                return
            const subjectId = subjectBox.currentValue || ""
            const priority = priorityBox.currentValue || "none"
            const ok = editor.itemId
                ? Homework.updateItem(editor.itemId, subjectId, content, priority)
                : Homework.addItem(subjectId, content, priority)
            if (ok)
                editor.close()
        }

        onRejected: editor.close()
    }
}
