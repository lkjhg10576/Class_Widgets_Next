import QtQuick
import QtQuick.Layouts
import RinUI
import ClassWidgets.Components

FluentPage {
    id: root
    title: qsTr("Plugins")
    horizontalPadding: 0
    wrapperWidth: width - 42 * 2

    readonly property string baseUrl: PlazaBridge.baseUrl
    property var plugins: []
    property var tags: []
    property string initialTag: ""
    property string activeTag: initialTag
    property string sort: "latest"
    property int page: 1
    property int perPage: 12
    property int total: 0
    property int totalPages: 1
    property bool loading: false
    property bool initialLoad: true
    property string errorMessage: ""
    property int requestSerial: 0
    property var activePluginRequest: null
    property var activeTagsRequest: null
    property var flickable: null
    readonly property int prefetchThreshold: 50
    readonly property bool hasMore: !loading && !initialLoad && page < totalPages && errorMessage.length === 0

    function request(url, callback, failure) {
        var xhr = new XMLHttpRequest()
        xhr.onreadystatechange = function() {
            if (xhr.readyState !== XMLHttpRequest.DONE)
                return
            if (xhr.status >= 200 && xhr.status < 300) {
                try { callback(JSON.parse(xhr.responseText)) }
                catch (error) { failure(qsTr("Invalid server response.")) }
            } else {
                failure(xhr.status + " " + xhr.statusText)
            }
        }
        xhr.open("GET", url)
        xhr.send()
        return xhr
    }

    function findFlickable() {
        var p = layout.parent
        while (p) {
            if (p.toString().indexOf("Flickable") !== -1) {
                flickable = p
                return
            }
            p = p.parent
        }
    }

    function loadPlugins(nextPage, append) {
        var serial = ++requestSerial
        if (activePluginRequest)
            activePluginRequest.abort()
        var targetPage = nextPage || 1
        var isAppend = append === true && targetPage > 1
        if (!isAppend) {
            plugins = []
            initialLoad = true
        }
        page = targetPage
        loading = true
        errorMessage = ""
        var endpoint = activeTag ? "/api/plugins/category" : "/api/plugins"
        var params = "?page=" + page + "&per_page=" + perPage + "&sort=" + encodeURIComponent(sort)
        if (activeTag) params += "&tag=" + encodeURIComponent(activeTag)
        activePluginRequest = request(baseUrl + endpoint + params, function(response) {
            if (serial !== requestSerial)
                return
            if (response.ok === false) {
                if (!isAppend) plugins = []
                total = 0
                totalPages = 1
                errorMessage = response.error || qsTr("The plaza rejected the request.")
            } else {
                var newItems = response.data instanceof Array ? response.data : []
                plugins = isAppend ? plugins.concat(newItems) : newItems
                total = response.meta && response.meta.total !== undefined ? response.meta.total : plugins.length
                totalPages = response.meta && response.meta.total_pages ? response.meta.total_pages : 1
            }
            loading = false
            initialLoad = false
        }, function(error) {
            if (serial !== requestSerial)
                return
            if (!isAppend) plugins = []
            total = 0
            totalPages = 1
            errorMessage = qsTr("Unable to load plugins: ") + error
            loading = false
            initialLoad = false
        })
    }

    function loadMore() {
        if (!hasMore)
            return
        loadPlugins(page + 1, true)
    }

    function checkLoadMore() {
        if (!flickable || !hasMore)
            return
        var remaining = flickable.contentHeight - (flickable.contentY + flickable.height)
        if (remaining <= prefetchThreshold)
            loadMore()
    }

    function loadTags() {
        if (activeTagsRequest)
            activeTagsRequest.abort()
        activeTagsRequest = request(baseUrl + "/api/plugins/tags", function(response) {
            tags = response.data instanceof Array ? response.data : []
        }, function() { tags = [] })
    }

    function selectTag(tagId) {
        if (activeTag === tagId)
            return
        activeTag = tagId
        loadPlugins(1, false)
    }

    function selectSort(value) {
        if (sort === value)
            return
        sort = value
        loadPlugins(1, false)
    }

    Component.onCompleted: {
        findFlickable()
        loadPlugins(1)
        loadTags()
    }

    Component.onDestruction: {
        ++requestSerial
        if (activePluginRequest)
            activePluginRequest.abort()
        if (activeTagsRequest)
            activeTagsRequest.abort()
    }

    Connections {
        target: root.flickable
        enabled: root.flickable !== null
        function onContentYChanged() { root.checkLoadMore() }
        function onContentHeightChanged() { root.checkLoadMore() }
    }

    ColumnLayout {
        id: layout
        Layout.fillWidth: true
        spacing: 16

        FilterToolbar {
            Layout.fillWidth: true
            visible: !root.initialLoad
            tags: root.tags
            selectedTag: root.activeTag
            sortValue: root.sort
            busy: root.loading
            onTagSelected: function(tagId) { root.selectTag(tagId) }
            onSortSelected: function(value) { root.selectSort(value) }
        }

        PlazaLoading {
            Layout.fillWidth: true
            visible: root.initialLoad
        }

        PluginGrid {
            Layout.fillWidth: true
            visible: !root.initialLoad && root.errorMessage.length === 0 && root.plugins.length > 0
            plugins: root.plugins
            loading: false
        }

        PlazaLoading {
            Layout.fillWidth: true
            Layout.topMargin: 4
            Layout.bottomMargin: 8
            visible: !root.initialLoad && root.loading && root.page > 1
        }

        ErrorState {
            Layout.fillWidth: true
            visible: !root.initialLoad && root.errorMessage.length > 0
            title: qsTr("Could not load plugins")
            description: root.errorMessage
            onRetryRequested: root.loadPlugins(root.page, root.page > 1)
        }

        EmptyState {
            Layout.fillWidth: true
            visible: !root.initialLoad && !root.loading && root.errorMessage.length === 0 && root.plugins.length === 0
            // icon.name: "ic_fluent_search_info_24_regular"
            title: qsTr("No plugins found")
            description: root.activeTag ? qsTr("Try another category.") : qsTr("The plaza is empty right now.")
        }
    }
}
