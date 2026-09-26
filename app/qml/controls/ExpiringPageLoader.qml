import QtQuick

// Owns only the view. Long-running services remain owned by the application.
Loader {
    id: root
    property bool pageActive: false
    property int retentionMs: 5 * 60 * 1000
    signal expired()
    active: false

    function updateLifetime() {
        expiry.stop()
        if (pageActive) active = true
        else if (active) expiry.restart()
    }
    onPageActiveChanged: updateLifetime()
    onRetentionMsChanged: updateLifetime()
    Component.onCompleted: updateLifetime()

    Timer {
        id: expiry
        interval: Math.max(1, root.retentionMs)
        repeat: false
        onTriggered: {
            if (root.pageActive || !root.active) return
            root.active = false
            root.expired()
        }
    }
}
