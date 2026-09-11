import QtQuick
import QtQuick.Layouts
import RinUI

RowLayout {
    id: root

    property var tags: []
    property string selectedTag: ""
    property string sortValue: "latest"
    property bool showRelevance: false
    property bool busy: false
    property var menuTags: []
    property bool menuOpenPending: false
    signal tagSelected(string tagId)
    signal sortSelected(string sortValue)

    readonly property var tagItems: root.tags instanceof Array ? root.tags : []
    readonly property int visibleTagCount: {
        var availableWidth = Math.max(120, root.width - 216)
        var usedWidth = root.estimatedTagWidth(qsTr("All"))
        var count = 0

        for (var i = 0; i < root.tagItems.length; ++i) {
            var tag = root.tagItems[i]
            var label = tag.name || tag.id || ""
            var nextWidth = root.estimatedTagWidth(label)
            if (usedWidth + nextWidth > availableWidth)
                break
            usedWidth += nextWidth
            ++count
        }

        return Math.max(1, count)
    }
    readonly property var baseVisibleTags: root.tagItems.slice(0, root.visibleTagCount)
    readonly property var activeHiddenTag: root.selectedTag === "" ? null : root.hiddenSelectedTag()
    readonly property var visibleTags: root.activeHiddenTag && root.baseVisibleTags.length > 0
        ? root.baseVisibleTags.slice(0, -1).concat([root.activeHiddenTag])
        : root.baseVisibleTags
    readonly property var hiddenTags: root.remainingTags()
    readonly property int selectedTagIndex: root.visibleTagIndex() + 1

    onSelectedTagChanged: scheduleSelectorSync()
    onVisibleTagsChanged: scheduleSelectorSync()

    function tagId(tag) {
        return tag.id || tag.name || ""
    }

    function estimatedTagWidth(label) {
        return Math.ceil(String(label).length * 14 + 32)
    }

    function openMoreTagsMenu() {
        menuTags = hiddenTags.slice()
        if (menuOpenPending || moreTagsMenu.opened)
            return

        menuOpenPending = true
        Qt.callLater(function() {
            menuOpenPending = false
            if (root.menuTags.length > 0 && !moreTagsMenu.opened)
                moreTagsMenu.open()
        })
    }

    function scheduleSelectorSync() {
        Qt.callLater(function() {
            tagSelector.currentIndex = root.selectedTagIndex
        })
    }

    function visibleTagIndex() {
        for (var i = 0; i < root.visibleTags.length; ++i) {
            if (root.tagId(root.visibleTags[i]) === root.selectedTag)
                return i
        }
        return -1
    }

    function hiddenSelectedTag() {
        for (var i = 0; i < root.tagItems.length; ++i) {
            var tag = root.tagItems[i]
            if (root.tagId(tag) === root.selectedTag && i >= root.baseVisibleTags.length)
                return tag
        }
        return null
    }

    function isVisibleTag(tag) {
        var id = root.tagId(tag)
        for (var i = 0; i < root.visibleTags.length; ++i) {
            if (root.tagId(root.visibleTags[i]) === id)
                return true
        }
        return false
    }

    function remainingTags() {
        var result = []
        for (var i = 0; i < root.tagItems.length; ++i) {
            if (!root.isVisibleTag(root.tagItems[i]))
                result.push(root.tagItems[i])
        }
        return result
    }

    spacing: 8

    RowLayout {
        spacing: 4

        SelectorBar {
            id: tagSelector
            enabled: !root.busy
            Component.onCompleted: root.scheduleSelectorSync()

            SelectorBarItem {
                text: qsTr("All")
                onClicked: root.tagSelected("")
            }

            Repeater {
                model: root.visibleTags

                delegate: SelectorBarItem {
                    width: implicitWidth
                    required property var modelData
                    readonly property string tagId: root.tagId(modelData)
                    text: modelData.name || modelData.id || ""
                    onClicked: root.tagSelected(tagId)
                }
            }
        }

        ToolButton {
            visible: root.hiddenTags.length > 0
            enabled: !root.busy
            flat: true
            icon.name: "ic_fluent_more_horizontal_20_regular"
            onClicked: root.openMoreTagsMenu()

            ToolTip {
                visible: parent.hovered
                text: qsTr("More categories")
            }

            Menu {
                id: moreTagsMenu
                height: implicitHeight

                // RinUI's default enter transition snapshots implicitHeight before
                // dynamically inserted menu items have completed layout.
                enter: Transition {
                    NumberAnimation {
                        property: "opacity"
                        from: 0
                        to: 1
                        duration: 100
                    }
                }

                MenuItem {
                    visible: root.menuTags.length > 0
                    text: root.menuTags.length > 0 ? root.menuTags[0].name || root.menuTags[0].id || "" : ""
                    onTriggered: { moreTagsMenu.close(); root.tagSelected(root.tagId(root.menuTags[0])) }
                }
                MenuItem {
                    visible: root.menuTags.length > 1
                    text: root.menuTags.length > 1 ? root.menuTags[1].name || root.menuTags[1].id || "" : ""
                    onTriggered: { moreTagsMenu.close(); root.tagSelected(root.tagId(root.menuTags[1])) }
                }
                MenuItem {
                    visible: root.menuTags.length > 2
                    text: root.menuTags.length > 2 ? root.menuTags[2].name || root.menuTags[2].id || "" : ""
                    onTriggered: { moreTagsMenu.close(); root.tagSelected(root.tagId(root.menuTags[2])) }
                }
                MenuItem {
                    visible: root.menuTags.length > 3
                    text: root.menuTags.length > 3 ? root.menuTags[3].name || root.menuTags[3].id || "" : ""
                    onTriggered: { moreTagsMenu.close(); root.tagSelected(root.tagId(root.menuTags[3])) }
                }
                MenuItem {
                    visible: root.menuTags.length > 4
                    text: root.menuTags.length > 4 ? root.menuTags[4].name || root.menuTags[4].id || "" : ""
                    onTriggered: { moreTagsMenu.close(); root.tagSelected(root.tagId(root.menuTags[4])) }
                }
                MenuItem {
                    visible: root.menuTags.length > 5
                    text: root.menuTags.length > 5 ? root.menuTags[5].name || root.menuTags[5].id || "" : ""
                    onTriggered: { moreTagsMenu.close(); root.tagSelected(root.tagId(root.menuTags[5])) }
                }
                MenuItem {
                    visible: root.menuTags.length > 6
                    text: root.menuTags.length > 6 ? root.menuTags[6].name || root.menuTags[6].id || "" : ""
                    onTriggered: { moreTagsMenu.close(); root.tagSelected(root.tagId(root.menuTags[6])) }
                }
                MenuItem {
                    visible: root.menuTags.length > 7
                    text: root.menuTags.length > 7 ? root.menuTags[7].name || root.menuTags[7].id || "" : ""
                    onTriggered: { moreTagsMenu.close(); root.tagSelected(root.tagId(root.menuTags[7])) }
                }
                MenuItem {
                    visible: root.menuTags.length > 8
                    text: root.menuTags.length > 8 ? root.menuTags[8].name || root.menuTags[8].id || "" : ""
                    onTriggered: { moreTagsMenu.close(); root.tagSelected(root.tagId(root.menuTags[8])) }
                }
                MenuItem {
                    visible: root.menuTags.length > 9
                    text: root.menuTags.length > 9 ? root.menuTags[9].name || root.menuTags[9].id || "" : ""
                    onTriggered: { moreTagsMenu.close(); root.tagSelected(root.tagId(root.menuTags[9])) }
                }
            }
        }
    }

    Item { Layout.fillWidth: true }

    DropDownButton {
        Layout.alignment: Qt.AlignRight
        enabled: !root.busy
        text: root.sortValue === "relevance" ? qsTr("Relevance")
            : root.sortValue === "name" ? qsTr("Name")
            : root.sortValue === "rating" ? qsTr("Rating")
            : root.sortValue === "downloads" ? qsTr("Downloads")
            : qsTr("Latest")
        icon.name: "ic_fluent_arrow_sort_20_regular"

        MenuItem { visible: root.showRelevance; text: qsTr("Relevance"); onTriggered: root.sortSelected("relevance") }
        MenuItem { text: qsTr("Latest"); onTriggered: root.sortSelected("latest") }
        MenuItem { text: qsTr("Name"); onTriggered: root.sortSelected("name") }
        MenuItem { text: qsTr("Rating"); onTriggered: root.sortSelected("rating") }
        MenuItem { text: qsTr("Downloads"); onTriggered: root.sortSelected("downloads") }
    }
}
