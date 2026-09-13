import QtQuick

Loader {
    id: loader
    property string widgetSource: model.qmlPath
    property bool reloading: false
    source: widgetSource
    asynchronous: true
    // width: item ? item.implicitWidth : 0
    // height: item ? item.height : 0
    onStatusChanged: {
        if (status === Loader.Ready) {
            reloading = false
            if (item && model.backendObj) {
                item.backend = model.backendObj
            }
            if (item && model.settings) {
                item.settings = model.settings
            }
            if (item && item.hasOwnProperty('instanceId')) {
                item.instanceId = model.instanceId
            }
            if (item && item.hasOwnProperty('widget_id')) {
                item.widget_id = model.widget_id
            }
            if (item && item.hasOwnProperty('editMode')) {
                item.editMode = widgetsContainer.editMode
            }
            anim.start()
        } else if (status === Loader.Error) {
            console.error("Unable to load widget:", model.typeId, widgetSource)
            // A themed component can fail while the Loader source itself is
            // a widget/plugin URL, so the source path cannot identify this
            // as a theme failure.
            AppCentral.reportThemeLoadFailure(widgetSource)
        }
    }

    Connections {
        target: WidgetsModel
        function onModelChanged() {
            if (loader.item && model.settings) {
                loader.item.settings = model.settings
            }
            if (loader.item && loader.item.hasOwnProperty('instanceId')) {
                loader.item.instanceId = model.instanceId
            }
            if (loader.item && loader.item.hasOwnProperty('widget_id')) {
                loader.item.widget_id = model.widget_id
            }
        }
    }

    Connections {
        target: widgetsContainer
        function onEditModeChanged() {
            if (loader.item && loader.item.hasOwnProperty('editMode')) {
                loader.item.editMode = widgetsContainer.editMode
            }
        }
    }

    // Connections {
    //     target: CWThemeManager
    //     function onThemeChanged() {
    //         if (reloading) {
    //             console.log("WidgetLoader: Already reloading, skip:", model.name)
    //             return
    //         }
    //         console.log("WidgetLoader: Theme changed signal received, will reload:", model.name)
    //     }
    // }

    Connections {
        target: CWThemeManager
        // 感谢gemini的超强research，要不然我一辈子都solve不了
        // B3 缓存纪律修订（2026-09-13）：不再给 URL 叠加 ?t=Date.now()。
        // 时间戳使每次主题切换的 widget URL 都成为新地址，击穿类型缓存与
        // QML 磁盘缓存（键 = 拦截后 URL），等于每次切主题全量重编译。
        // 主题组件的更新本就由 ThemeUrlInterceptor 按"主题目录 + 文件指纹"
        // 改写 URL 完成；widget 文件自身 URL 保持稳定，缓存得以复用。
        // 强制重建 item 的动作保留：source 置空 → 下一拍恢复，Loader 销毁并重建。
        function onThemeReadyToReload() {
            if (reloading) return

            reloading = true
            source = ""

            Qt.callLater(function() {
                source = widgetSource
            })
        }
    }
}
