import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FluentUI
import "controls"

FluWindow {
    id: window
    width: 1100
    height: 700
    minimumWidth: 720
    minimumHeight: 460
    launchMode: FluWindowType.SingleTask
    title: LivePlayerController.title || qsTr("直播播放器")
    readonly property bool fullscreenActive: visibility === Window.FullScreen
    property int previousVisibility: Window.Windowed
    property int previousBarHeight: 48
    property bool controlsShown: true
    property bool cursorHidden: false
    readonly property bool controlsLocked: top_hover.hovered || bottom_hover.hovered
        || top_bar.activeFocus || bottom_bar.activeFocus || volume_slider.pressed
        || quality_menu.visible || LivePlayerController.paused || LivePlayerController.loading
        || LivePlayerController.buffering || LivePlayerController.errorMessage !== ""
        || LivePlayerController.roomId === ""
    onControlsLockedChanged: {
        pokeControls()
        if (!controlsLocked && !video_click.containsMouse) scheduleQuickHide()
    }
    function pokeControls() {
        controlsShown = true
        cursorHidden = false
        quick_hide.stop()
        hide_timer.restart()
        cursor_timer.restart()
    }
    function finishAction() {
        root.forceActiveFocus()
        pokeControls()
    }
    function scheduleQuickHide() {
        cursorHidden = false
        cursor_timer.stop()
        if (!controlsLocked) {
            hide_timer.stop()
            quick_hide.restart()
        }
    }
    function toggleFullscreen() {
        if (fullscreenActive) {
            appBar.visible = true
            appBar.height = previousBarHeight
            visibility = previousVisibility
        } else {
            previousVisibility = visibility === Window.Maximized ? Window.Maximized : Window.Windowed
            previousBarHeight = appBar.height
            appBar.visible = false
            appBar.height = 0
            visibility = Window.FullScreen
        }
        finishAction()
    }
    function unmount() {
        LivePlayerController.videoItem.parent = null
    }
    closeListener: function(event) {
        quality_menu.close()
        unmount()
        LivePlayerController.closeRequested()
        if (window.autoDestroy) FluRouter.removeWindow(window)
        else {
            window.visibility = Window.Hidden
            event.accepted = false
        }
    }
    Component.onDestruction: unmount()
    Timer {
        id: hide_timer
        interval: 3000
        onTriggered: if (!window.controlsLocked) window.controlsShown = false
    }
    Timer {
        id: quick_hide
        interval: 600
        onTriggered: if (!window.controlsLocked) window.controlsShown = false
    }
    Timer {
        id: cursor_timer
        interval: 5000
        onTriggered: if (!window.controlsLocked && video_click.containsMouse) window.cursorHidden = true
    }
    Connections {
        target: LivePlayerController
        function onRoomChanged() { window.finishAction() }
    }

    Item {
        id: root
        anchors.fill: parent
        focus: true
        // 未被菜单/滑块/按钮消费的按键才作为播放快捷键，与标准窗口一致。
        Keys.onPressed: function(event) {
            if (quality_menu.visible) return
            switch (event.key) {
            case Qt.Key_F:
                window.toggleFullscreen()
                event.accepted = true
                break
            case Qt.Key_Escape:
                if (window.fullscreenActive) window.toggleFullscreen()
                event.accepted = true
                break
            case Qt.Key_Space:
                LivePlayerController.togglePlayPause()
                event.accepted = true
                break
            }
            if (event.accepted) window.pokeControls()
        }
        Rectangle {
            id: video_holder
            anchors.fill: parent
            color: "black"
            clip: true
            Component.onCompleted: {
                if (Qt.platform.pluginName !== "offscreen") {
                    LivePlayerController.videoItem.z = 0
                    LivePlayerController.videoItem.parent = video_holder
                    LivePlayerController.videoItem.anchors.fill = video_holder
                    LivePlayerController.videoItem.visible = true
                }
                window.pokeControls()
            }
        }
        MouseArea {
            id: video_click
            z: 1
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: window.cursorHidden ? Qt.BlankCursor : Qt.ArrowCursor
            onClicked: window.finishAction()
            onDoubleClicked: window.toggleFullscreen()
            onPositionChanged: window.pokeControls()
            onExited: window.scheduleQuickHide()
        }
        Column {
            z: 2
            anchors.centerIn: parent
            width: Math.min(parent.width - 48, 560)
            spacing: 14
            visible: LivePlayerController.loading || LivePlayerController.buffering
                     || LivePlayerController.errorMessage !== "" || LivePlayerController.roomId === ""
            FluProgressRing {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: LivePlayerController.loading || LivePlayerController.buffering
            }
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: "white"
                text: LivePlayerController.errorMessage || LivePlayerController.statusText || qsTr("选择直播间开始播放")
            }
            FluButton {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: LivePlayerController.errorMessage !== ""
                text: qsTr("重新连接")
                onClicked: { LivePlayerController.retry(); window.finishAction() }
            }
        }
        Item {
            id: overlay
            z: 3
            anchors.fill: parent
            opacity: window.controlsShown ? 1 : 0
            visible: opacity > 0.01
            enabled: window.controlsShown
            Behavior on opacity { NumberAnimation { duration: 200 } }

            FocusScope {
                id: top_bar
                anchors { left: parent.left; right: parent.right; top: parent.top }
                height: 72
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        GradientStop { position: 0; color: "#b8000000" }
                        GradientStop { position: 1; color: "transparent" }
                    }
                }
                MouseArea { anchors.fill: parent; onClicked: window.finishAction() }
                HoverHandler { id: top_hover; onPointChanged: window.pokeControls() }
                RowLayout {
                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12 }
                    spacing: 12
                    Rectangle { width: 8; height: 8; radius: 4; color: "#e13b73" }
                    FluText {
                        Layout.fillWidth: true
                        text: LivePlayerController.authorName || qsTr("直播")
                        elide: Text.ElideRight
                        font.bold: true
                        color: "#f5f5f5"
                    }
                    FluText {
                        text: LivePlayerController.roomId ? qsTr("房间 %1").arg(LivePlayerController.roomId) : ""
                        font.pixelSize: 12
                        color: "#eeeeee"
                    }
                    PlayerControlButton {
                        caption: qsTr("浏览器打开")
                        contentDescription: caption
                        enabled: LivePlayerController.roomId !== ""
                        onClicked: {
                            Qt.openUrlExternally("https://live.bilibili.com/" + LivePlayerController.roomId)
                            window.finishAction()
                        }
                    }
                }
            }
            Rectangle {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: bottom_bar.height + 44
                gradient: Gradient {
                    GradientStop { position: 0; color: "transparent" }
                    GradientStop { position: 0.45; color: "#73000000" }
                    GradientStop { position: 1; color: "#e0000000" }
                }
            }
            FocusScope {
                id: bottom_bar
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom
                    leftMargin: 16; rightMargin: 16; bottomMargin: 8 }
                height: 74
                MouseArea { anchors.fill: parent; onClicked: window.finishAction() }
                HoverHandler { id: bottom_hover; onPointChanged: window.pokeControls() }
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        FluText {
                            Layout.fillWidth: true
                            text: LivePlayerController.statusText
                            elide: Text.ElideRight
                            font.pixelSize: 12
                            color: "#eeeeee"
                        }
                        FluText {
                            text: qsTr("直播无进度条 · 重新连接可返回最新画面")
                            font.pixelSize: 12
                            color: "#eeeeee"
                            visible: window.width >= 880
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        PlayerControlButton {
                            iconSource: LivePlayerController.paused ? FluentIcons.Play : FluentIcons.Pause
                            contentDescription: LivePlayerController.paused ? qsTr("播放") : qsTr("暂停")
                            enabled: LivePlayerController.roomId !== "" && !LivePlayerController.loading
                                     && LivePlayerController.errorMessage === ""
                            onClicked: { LivePlayerController.togglePlayPause(); window.finishAction() }
                        }
                        PlayerControlButton {
                            iconSource: FluentIcons.Refresh
                            contentDescription: qsTr("重新连接 / 回到直播")
                            enabled: LivePlayerController.roomId !== "" && !LivePlayerController.loading
                            onClicked: { LivePlayerController.retry(); window.finishAction() }
                        }
                        PlayerControlButton {
                            iconSource: LivePlayerController.volumePercent === 0 ? FluentIcons.Mute : FluentIcons.Volume
                            contentDescription: qsTr("静音")
                            property int savedVolume: 80
                            onClicked: {
                                if (LivePlayerController.volumePercent > 0) {
                                    savedVolume = LivePlayerController.volumePercent
                                    LivePlayerController.setVolumePercent(0)
                                } else LivePlayerController.setVolumePercent(savedVolume)
                                window.finishAction()
                            }
                        }
                        FluSlider {
                            id: volume_slider
                            Layout.preferredWidth: 100
                            from: 0; to: 100
                            focusPolicy: Qt.TabFocus
                            Binding {
                                target: volume_slider
                                property: "value"
                                value: LivePlayerController.volumePercent
                                when: !volume_slider.pressed
                                restoreMode: Binding.RestoreNone
                            }
                            onMoved: {
                                LivePlayerController.setVolumePercent(Math.round(value))
                                window.pokeControls()
                            }
                            onPressedChanged: if (!pressed) window.finishAction()
                            Accessible.name: qsTr("音量")
                            background: Rectangle {
                                x: volume_slider.leftPadding + 5
                                y: (volume_slider.height - height) / 2
                                width: Math.max(0, volume_slider.availableWidth - 10)
                                height: 3; radius: 2; color: "#40ffffff"
                                Rectangle { width: volume_slider.position * parent.width; height: 3; radius: 2; color: "white" }
                            }
                            handle: Rectangle {
                                width: 10; height: 10; radius: 5; color: "white"
                                x: volume_slider.leftPadding + volume_slider.visualPosition * (volume_slider.availableWidth - width)
                                y: (volume_slider.height - height) / 2
                            }
                        }
                        Item { Layout.fillWidth: true }
                        PlayerControlButton {
                            id: quality_button
                            width: 140
                            caption: LivePlayerController.qualityLabel || qsTr("清晰度")
                            contentDescription: qsTr("清晰度")
                            enabled: LivePlayerController.qualities.length > 0 && !LivePlayerController.loading
                            onClicked: { window.pokeControls(); quality_menu.open() }
                        }
                        PlayerControlButton {
                            iconSource: window.fullscreenActive ? FluentIcons.BackToWindow : FluentIcons.FullScreen
                            contentDescription: window.fullscreenActive ? qsTr("退出全屏") : qsTr("全屏")
                            onClicked: window.toggleFullscreen()
                        }
                    }
                }
                FluMenu {
                    id: quality_menu
                    parent: bottom_bar
                    width: 224
                    padding: 6
                    x: Math.max(0, Math.min(bottom_bar.width - width, quality_button.mapToItem(bottom_bar, quality_button.width, 0).x - width))
                    y: -height - 10
                    height: Math.min(implicitHeight, Math.max(100, root.height - bottom_bar.height - top_bar.height - 24))
                    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                    contentItem: ListView {
                        implicitHeight: contentHeight
                        model: quality_menu.contentModel
                        currentIndex: quality_menu.currentIndex
                        clip: true
                        interactive: contentHeight > height
                        ScrollBar.vertical: FluScrollBar {}
                    }
                    background: Rectangle { radius: 12; color: "#fa202020"; border.color: "#33ffffff" }
                    onOpened: window.pokeControls()
                    onClosed: window.finishAction()
                    Repeater {
                        model: LivePlayerController.qualities
                        FluMenuItem {
                            id: quality_item
                            required property var modelData
                            implicitHeight: 36
                            text: modelData.label
                            checkable: true
                            checked: modelData.qn === LivePlayerController.currentQn
                            contentItem: FluText {
                                text: quality_item.text
                                font.pixelSize: 13
                                color: quality_item.checked ? "#ff4d4f" : "#f5f5f5"
                                leftPadding: 26; rightPadding: 8
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }
                            indicator: FluIcon {
                                x: 10
                                anchors.verticalCenter: parent.verticalCenter
                                iconSource: FluentIcons.CheckMark
                                iconSize: 13
                                iconColor: "#ff4d4f"
                                visible: quality_item.checked
                            }
                            background: Rectangle {
                                radius: 8
                                color: quality_item.highlighted || quality_item.hovered ? "#26ffffff"
                                    : quality_item.checked ? "#20ff4d4f" : "transparent"
                            }
                            onTriggered: LivePlayerController.setQuality(modelData.qn)
                        }
                    }
                }
            }
        }
    }
}
