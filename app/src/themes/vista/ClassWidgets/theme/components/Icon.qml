import QtQuick
import QtQuick.Effects

Item {
    id: root

    // Keep the public Icon API used by widgets and plugins.
    property string name: ""
    property string icon: ""
    property string source: ""
    property real size: 24
    property color color: "white"

    implicitWidth: size
    implicitHeight: size
    width: size
    height: size

    function assetForName(iconName) {
        var key = (iconName || "").toLowerCase()
        if (key === "free" || key.indexOf("accessibility") !== -1)
            return Qt.resolvedUrl("../assets/Free.png")
        if (key === "break" || key.indexOf("shifts_activity") !== -1)
            return Qt.resolvedUrl("../assets/Break.png")
        if (key === "class" || key.indexOf("class") !== -1)
            return Qt.resolvedUrl("../assets/Class.png")
        if (key === "activity" || key.indexOf("alert") !== -1)
            return Qt.resolvedUrl("../assets/Activity.png")
        if (key === "preparation" || key.indexOf("hourglass") !== -1)
            return Qt.resolvedUrl("../assets/Class.png")
        if (key === "preparation" || key.indexOf("alert") !== -1)
            return Qt.resolvedUrl("../assets/Notification.png")
        return Qt.resolvedUrl("../assets/Class.png")
    }

    readonly property string fallbackSource: Qt.resolvedUrl("../assets/Activity.png")
    readonly property string resolvedSource: source || assetForName(name || icon)
    property string imageSource: resolvedSource

    onResolvedSourceChanged: imageSource = resolvedSource

    Image {
        id: image
        anchors.fill: parent
        source: root.imageSource
        fillMode: Image.PreserveAspectFit
        smooth: true
        asynchronous: true
        mipmap: true
        onStatusChanged: {
            if (status === Image.Error && root.imageSource !== root.fallbackSource)
                root.imageSource = root.fallbackSource
        }
    }

    layer.enabled: true
    layer.effect: MultiEffect {
        shadowEnabled: true
        shadowHorizontalOffset: 0
        shadowVerticalOffset: 3
        shadowBlur: 0.32
        shadowColor: Qt.alpha("black", 0.25)
    }
}
