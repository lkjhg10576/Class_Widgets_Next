import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RinUI
import Qt5Compat.GraphicalEffects
import ClassWidgets.Components


FluentWindow {
    id: root
    icon: PathManager.assets("images/icons/cwn_plugin.png")
    title: qsTr("Plugin Plaza")
    width: Screen.width * 0.67
    height: Screen.height * 0.69
    minimumWidth: 900

    function formatBytes(bytes) {
        var value = Number(bytes) || 0
        if (value < 1024)
            return qsTr("%1 B").arg(value)
        if (value < 1024 * 1024)
            return qsTr("%1 KB").arg((value / 1024).toFixed(1))
        return qsTr("%1 MB").arg((value / 1024 / 1024).toFixed(1))
    }

    onClosing: function(event) {
        event.accepted = false
        WindowManager.closePlaza()
    }

    titleBarArea: RowLayout {
        anchors.fill: parent
        // spacing: 24


        // search field
        AutoSuggestBox {
            id: searchField
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: 600
            Layout.fillWidth: true
            placeholderText: qsTr("Search plugins...")
            property var suggestionItems: []
            property var chosenSuggestion: null
            property int requestSerial: 0
            property var activeSuggestionRequest: null
            property bool updatingSuggestions: false
            suggestions: suggestionItems
            textRole: "label"

            Timer {
                id: suggestionDebounce
                interval: 250
                repeat: false
                onTriggered: searchField.refreshSuggestions()
            }

            function refreshSuggestions() {
                var keyword = text.trim()
                if (!keyword) {
                    suggestionItems = []
                    return
                }

                var serial = ++requestSerial
                if (activeSuggestionRequest)
                    activeSuggestionRequest.abort()
                var xhr = new XMLHttpRequest()
                activeSuggestionRequest = xhr
                xhr.onreadystatechange = function() {
                    if (xhr.readyState !== XMLHttpRequest.DONE || serial !== requestSerial)
                        return
                    activeSuggestionRequest = null
                    if (xhr.status < 200 || xhr.status >= 300) {
                        suggestionItems = []
                        return
                    }
                    try {
                        var response = JSON.parse(xhr.responseText)
                        var items = response.ok !== false && response.data instanceof Array
                                ? response.data : []
                        if (text.trim() !== keyword)
                            return
                        suggestionItems = items

                        // AutoSuggestBox filters when its text changes, so refresh the
                        // built-in filtered model after the asynchronous response arrives.
                        updatingSuggestions = true
                        userInput = false
                        text = ""
                        userInput = true
                        text = keyword
                        updatingSuggestions = false
                    } catch (error) {
                        suggestionItems = []
                    }
                }
                xhr.open("GET", PlazaBridge.baseUrl + "/api/plugins/suggest?q="
                         + encodeURIComponent(keyword) + "&limit=8")
                xhr.send()
            }

            function submitSearch(keyword) {
                var query = keyword.trim()
                if (!query)
                    return
                var searchPageUrl = PathManager.qml("pages/plaza/Search.qml")
                var stack = navigationView.navigationBar.stackView
                if (navigationView.currentPage === searchPageUrl
                    && stack && stack.currentItem
                    && stack.currentItem.query !== undefined) {
                    var page = stack.currentItem
                    if (page.query === query)
                        page.search(1, false)
                    else
                        page.query = query
                    return
                }
                navigationView.push(searchPageUrl, { query: query })
            }

            onTextChanged: {
                if (updatingSuggestions)
                    return
                chosenSuggestion = null
                if (text.trim())
                    suggestionDebounce.restart()
                else {
                    suggestionDebounce.stop()
                    ++requestSerial
                    if (activeSuggestionRequest)
                        activeSuggestionRequest.abort()
                    activeSuggestionRequest = null
                    suggestionItems = []
                }
            }

            Component.onDestruction: {
                ++requestSerial
                if (activeSuggestionRequest)
                    activeSuggestionRequest.abort()
                activeSuggestionRequest = null
            }

            onSuggestionChosen: function(label) {
                for (var i = 0; i < suggestionItems.length; ++i) {
                    if (suggestionItems[i] && suggestionItems[i].label === label) {
                        chosenSuggestion = suggestionItems[i]
                        return
                    }
                }
            }

            onAccepted: {
                var keyword = text.trim()
                if (!keyword)
                    return

                if (chosenSuggestion && chosenSuggestion.type === "plugin" && chosenSuggestion.pluginId) {
                    navigationView.push(PathManager.qml("pages/plaza/Plugin.qml"), { pluginId: chosenSuggestion.pluginId })
                } else {
                    submitSearch(chosenSuggestion && chosenSuggestion.value ? chosenSuggestion.value : keyword)
                }
                chosenSuggestion = null
            }
        }



        ProgressRing {
            Layout.alignment: Qt.AlignRight
            visible: PluginManager.plazaInstallActive
            strokeWidth: 3
            size: 20
            value: PluginManager.installProgress / 100
            backgroundColor: indeterminate ? "transparent" : Colors.proxy.controlAltTertiaryColor
            indeterminate: PluginManager.installStatus === "Installing"

            ToolTip {
                visible: parent.hovered
                text: PluginManager.installTotalBytes > 0
                        ? qsTr("Downloaded: %1 / %2")
                          .arg(root.formatBytes(PluginManager.installDownloadedBytes))
                          .arg(root.formatBytes(PluginManager.installTotalBytes))
                        : qsTr("Downloading")
            }
        }

        RestartButton {
            Layout.alignment: Qt.AlignRight
        }
    }

    Component.onCompleted: {
        PlazaBridge.refreshAll()
    }

    Connections {
        target: PlazaBridge
        function onErrorOccurred(msg) {
            floatLayer.createInfoBar({
                title: qsTr("Error"),
                text: msg,
                severity: Severity.Error,
                timeout: 5000
            })
        }
    }

    Connections {
        target: PluginManager

        function pluginName(pluginId) {
            var plugins = PluginManager.plugins || []
            for (var index = 0; index < plugins.length; ++index) {
                if (plugins[index].id === pluginId)
                    return plugins[index].name || pluginId
            }
            return pluginId
        }

        function onPlazaTransferSucceeded(pluginId, version, kind) {
            var action = kind === "update" ? qsTr("Plugin updated") : qsTr("Plugin installed")
            floatLayer.createInfoBar({
                title: action,
                text: qsTr("%1 v%2 is ready to use.").arg(pluginName(pluginId)).arg(version),
                severity: Severity.Success,
                timeout: 5000
            })
        }

        function onPlazaTransferFailed(pluginId, message, kind) {
            var title = kind === "update" ? qsTr("Plugin update failed") : qsTr("Plugin installation failed")
            floatLayer.createInfoBar({
                title: title,
                text: message,
                severity: Severity.Error,
                timeout: -1
            })
        }

        function onPlazaTransferCancelled(pluginId) {
            floatLayer.createInfoBar({
                title: qsTr("Download cancelled"),
                text: qsTr("The download for %1 was cancelled.").arg(pluginName(pluginId)),
                severity: Severity.Info,
                timeout: 4000
            })
        }

        function onShowPlazaDownloadsRequested() {
            navigationView.push(PathManager.qml("pages/plaza/Downloads.qml"))
        }
    }

    navigationItems: [
        {
            title: qsTr("Home"),
            page: PathManager.qml("pages/plaza/Home.qml"),
            icon: "ic_fluent_home_20_regular",
        },
        {
            title: qsTr("Plugins"),
            page: PathManager.qml("pages/plaza/Plugins.qml"),
            icon: "ic_fluent_apps_list_20_regular",
        },
        {
            title: qsTr("Search"),
            page: PathManager.qml("pages/plaza/Search.qml"),
            icon: "ic_fluent_search_20_regular",
        },
        {
            title: qsTr("Downloads"),
            page: PathManager.qml("pages/plaza/Downloads.qml"),
            icon: "ic_fluent_cloud_arrow_down_20_regular",
            position: Position.Bottom
        }
    ]



    Watermark {
        anchors.centerIn: parent
    }
}
