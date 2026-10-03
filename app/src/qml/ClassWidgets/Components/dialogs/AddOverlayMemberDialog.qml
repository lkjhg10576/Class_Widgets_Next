import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RinUI

// 堆叠 overlay 成员新增对话框（four-plugins B 二期挂载点）。
// 上游 More_settings 堆叠 overlay 最复杂：独占行 / 就地编辑行 / presets 摆放 /
// 右键拦截 / removeInstance 卸载保护（C++ 侧已在 WidgetsModel 落地：
// OverlayMemberRole + overlayEditingId/overlayListMode + _overlayLocked 拒绝删除）。
// 本对话框为二期新增文件的最小可用版：一期仅支持“把当前预设某实例标为 overlay 成员
// （写 settings._overlayMember）+ presets 摆放说明”；独占行渲染与就地编辑行由
// WidgetsContainer 后续二期任务接入 overlayMember role 后生效。
Dialog {
    id: root
    title: qsTr("添加堆叠成员")
    modal: true
    standardButtons: Dialog.Ok | Dialog.Cancel

    property string targetInstanceId: ""

    ColumnLayout {
        spacing: 8
        width: parent ? parent.width : 320
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.currentTheme.colors.textColor
            text: qsTr("将某个小组件实例标为堆叠成员（独占行）。二期完整版将支持就地编辑行与摆放预设。")
        }
        ComboBox {
            id: instanceBox
            Layout.fillWidth: true
            model: WidgetsModel
            textRole: "instanceId"
            Component.onCompleted: {
                // 经 ComboBox.find 按 textRole（instanceId）定位初选——不走
                // QAbstractItemModel::rowCount/index/data（质检修正：三者是
                // C++ 虚函数而非 slot/Q_INVOKABLE，QML 调用即 TypeError）
                if (root.targetInstanceId !== "")
                    currentIndex = instanceBox.find(root.targetInstanceId)
            }
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.currentTheme.colors.textSecondaryColor
            text: qsTr("presets 摆放：成员标记经 settings._overlayMember 持久化，随预设保存。")
        }
    }

    onAccepted: {
        // currentText 即 textRole（instanceId）当前值，无需模型 data() 调用
        const id = instanceBox.currentText
        if (!id) return
        WidgetsModel.updateSettings(id, { "_overlayMember": true })
    }
}
