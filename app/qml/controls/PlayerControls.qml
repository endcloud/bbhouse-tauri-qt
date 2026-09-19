import QtQuick
import QtQuick.Controls
import FluentUI

// 仅负责呈现和转发动作；保持 PlayerController 作为实际播放状态的唯一来源。
FocusScope {
    id: controls
    property var controller: PlayerController
    property bool fullscreen: false
    property bool playlistExpanded: false
    property real popupMaxHeight: 360
    readonly property color accent: "#ff4d4f"
    readonly property bool compact: width < 810
    readonly property bool hasNext: controller.currentIndex >= 0
                                   && controller.currentIndex + 1 < controller.playlist.length
    readonly property bool menuOpen: quality_menu.visible || speed_menu.visible || episode_menu.visible || subtitle_menu.visible
    readonly property bool interacting: menuOpen || seek.pressed || volume_slider.pressed
                                       || control_hover.hovered || activeFocus
    property int lastAudibleVolume: 100
    implicitHeight: compact ? 114 : 74
    signal activity()
    signal actionTriggered()
    signal fullscreenRequested()
    signal playlistRequested()

    function formatTime(seconds) {
        var total = Math.max(0, Math.floor(seconds || 0))
        var hours = Math.floor(total / 3600)
        var minutes = Math.floor(total / 60) % 60
        var remainder = total % 60
        return (hours > 0 ? hours + ":" + (minutes < 10 ? "0" : "") : "")
                + minutes + ":" + (remainder < 10 ? "0" : "") + remainder
    }
    function toggleMute() {
        if (controller.volumePercent > 0) {
            lastAudibleVolume = controller.volumePercent
            controller.setVolumePercent(0)
        } else {
            controller.setVolumePercent(Math.max(1, lastAudibleVolume))
        }
        activity()
    }
    function cancelInteraction() {
        seek_throttle.stop()
        quality_menu.close()
        speed_menu.close()
        episode_menu.close()
        subtitle_menu.close()
    }
    onInteractingChanged: activity()
    HoverHandler {
        id: control_hover
        onPointChanged: controls.activity()
    }
    // 吃掉控制栏空白区点击，防止穿透成画面播放/暂停。
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.LeftButton; onClicked: controls.activity() }

    component DarkMenu: FluMenu {
        id: menu
        property Item trigger
        parent: controls
        width: 224
        padding: 6
        x: Math.max(0, Math.min(controls.width - width, trigger.mapToItem(controls, trigger.width, 0).x - width))
        y: trigger.mapToItem(controls, 0, 0).y - height - 10
        height: Math.min(implicitHeight, controls.popupMaxHeight)
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        contentItem: ListView {
            implicitHeight: contentHeight
            model: menu.contentModel
            currentIndex: menu.currentIndex
            clip: true
            interactive: contentHeight > height
            ScrollBar.vertical: FluScrollBar {}
        }
        background: Rectangle {
            radius: 12
            color: "#fa202020"
            border.color: "#33ffffff"
        }
        onOpened: controls.activity()
        onClosed: {
            controls.activity()
            controls.actionTriggered()
        }
    }
    component DarkMenuItem: FluMenuItem {
        id: menuItem
        implicitHeight: 36
        textColor: !enabled ? "#808080" : checked ? controls.accent : "#f5f5f5"
        contentItem: FluText {
            text: menuItem.text
            font.pixelSize: 13
            color: menuItem.textColor
            leftPadding: 26
            rightPadding: 8
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        indicator: FluIcon {
            x: 10
            anchors.verticalCenter: parent.verticalCenter
            iconSource: FluentIcons.CheckMark
            iconSize: 13
            iconColor: controls.accent
            visible: menuItem.checked
        }
        background: Rectangle {
            radius: 8
            color: menuItem.highlighted || menuItem.hovered ? "#26ffffff"
                         : menuItem.checked ? "#20ff4d4f" : "transparent"
        }
    }

    FluSlider {
        id: seek
        objectName: "playerSeekSlider"
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 24
        padding: 6
        from: 0
        to: Math.max(1, controls.controller.duration)
        stepSize: 0
        focusPolicy: Qt.TabFocus
        enabled: controls.controller.seekable && controls.controller.duration > 0
        tooltipEnabled: false
        property real scrubValue: 0
        readonly property real hoverRatio: Math.max(0, Math.min(1,
                    (seek_hover.point.position.x - leftPadding - 6) / Math.max(1, availableWidth - 12)))
        Accessible.name: qsTr("播放进度")
        Binding {
            target: seek
            property: "value"
            value: controls.controller.position
            when: !seek.pressed
            restoreMode: Binding.RestoreNone
        }
        onMoved: {
            scrubValue = value
            controls.activity()
            if (pressed) {
                if (!seek_throttle.running) seek_throttle.start()
            } else {
                controls.controller.seek(value)
            }
        }
        onPressedChanged: {
            if (pressed) {
                scrubValue = value
                controls.activity()
            } else {
                seek_throttle.stop()
                if (enabled) controls.controller.seek(scrubValue)
                controls.activity()
            }
        }
        Keys.onLeftPressed: {
            controls.controller.seek(controls.controller.position - 5)
            controls.activity()
        }
        Keys.onRightPressed: {
            controls.controller.seek(controls.controller.position + 5)
            controls.activity()
        }
        HoverHandler { id: seek_hover }
        background: Rectangle {
            x: seek.leftPadding + 6
            y: (seek.height - height) / 2
            width: Math.max(0, seek.availableWidth - 12)
            height: seek.hovered || seek.pressed || seek.activeFocus ? 5 : 3
            radius: height / 2
            color: "#40ffffff"
            Behavior on height { NumberAnimation { duration: 120 } }
            Rectangle {
                width: seek.hoverRatio * parent.width
                height: parent.height
                radius: parent.radius
                color: "#40ffffff"
                visible: seek_hover.hovered
            }
            Rectangle {
                width: seek.position * parent.width
                height: parent.height
                radius: parent.radius
                color: seek.enabled ? controls.accent : "#808080"
            }
        }
        handle: Rectangle {
            width: 12; height: 12; radius: 6
            x: seek.leftPadding + seek.visualPosition * (seek.availableWidth - width)
            y: (seek.height - height) / 2
            color: controls.accent
            scale: seek.hovered || seek.pressed || seek.activeFocus ? 1 : 0
            Behavior on scale { NumberAnimation { duration: 120 } }
        }
        Rectangle {
            visible: seek.enabled && (seek_hover.hovered || seek.pressed)
            width: hover_time.implicitWidth + 16
            height: 26
            radius: 5
            color: "#ef202020"
            y: -height - 4
            x: Math.max(0, Math.min(seek.width - width,
                   (seek.pressed ? seek.handle.x + 6 : seek_hover.point.position.x) - width / 2))
            FluText {
                id: hover_time
                anchors.centerIn: parent
                font.pixelSize: 12
                color: "white"
                text: controls.formatTime(seek.pressed ? seek.scrubValue : seek.hoverRatio * controls.controller.duration)
            }
        }
    }
    Timer {
        id: seek_throttle
        interval: 200
        onTriggered: if (seek.pressed && seek.enabled) controls.controller.seek(seek.scrubValue)
    }

    Row {
        id: left_controls
        anchors { left: parent.left; bottom: parent.bottom }
        height: 40
        spacing: 2
        PlayerControlButton {
            objectName: "playerPlayButton"
            iconSource: controls.controller.paused ? FluentIcons.Play : FluentIcons.Pause
            contentDescription: controls.controller.paused ? qsTr("播放（空格）") : qsTr("暂停（空格）")
            enabled: controls.controller.kernelAvailable && controls.controller.currentIndex >= 0
            onClicked: { controls.controller.togglePlayPause(); controls.actionTriggered() }
        }
        PlayerControlButton {
            visible: controls.hasNext
            iconSource: FluentIcons.Next
            contentDescription: qsTr("下一个（Shift+N）")
            onClicked: { controls.controller.playNext(); controls.actionTriggered() }
        }
        Item {
            id: volume_group
            width: volume_button.width + volume_slider.width
            height: 36
            readonly property bool expanded: volume_hover.hovered || volume_slider.pressed
                                             || volume_slider.activeFocus || volume_button.activeFocus
            HoverHandler { id: volume_hover }
            PlayerControlButton {
                id: volume_button
                objectName: "playerMuteButton"
                iconSource: controls.controller.volumePercent > 0 ? FluentIcons.Volume : FluentIcons.Mute
                contentDescription: controls.controller.volumePercent > 0 ? qsTr("静音（M）") : qsTr("取消静音（M）")
                onClicked: { controls.toggleMute(); controls.actionTriggered() }
            }
            FluSlider {
                id: volume_slider
                anchors { left: volume_button.right; verticalCenter: parent.verticalCenter }
                width: volume_group.expanded ? 86 : 0
                height: 28
                visible: width > 0
                enabled: volume_group.expanded
                tooltipEnabled: false
                padding: 6
                from: 0; to: 100; stepSize: 1
                focusPolicy: Qt.TabFocus
                Accessible.name: qsTr("音量")
                Binding {
                    target: volume_slider; property: "value"
                    value: controls.controller.volumePercent
                    when: !volume_slider.pressed
                    restoreMode: Binding.RestoreNone
                }
                Behavior on width { NumberAnimation { duration: 180 } }
                onMoved: { controls.controller.setVolumePercent(Math.round(value)); controls.activity() }
                background: Rectangle {
                    x: volume_slider.leftPadding + 5
                    y: (volume_slider.height - height) / 2
                    width: Math.max(0, volume_slider.availableWidth - 10)
                    height: 3; radius: 2
                    color: "#40ffffff"
                    Rectangle { width: volume_slider.position * parent.width; height: 3; radius: 2; color: "white" }
                }
                handle: Rectangle {
                    width: 10; height: 10; radius: 5; color: "white"
                    x: volume_slider.leftPadding + volume_slider.visualPosition * (volume_slider.availableWidth - width)
                    y: (volume_slider.height - height) / 2
                }
            }
        }
        FluText {
            anchors.verticalCenter: parent.verticalCenter
            leftPadding: 8
            font.pixelSize: 12
            color: "#eeeeee"
            text: controls.formatTime(seek.pressed ? seek.scrubValue : controls.controller.position)
                  + " / " + controls.formatTime(controls.controller.duration)
        }
    }
    Row {
        id: right_controls
        anchors { right: parent.right; bottom: parent.bottom; bottomMargin: controls.compact ? 40 : 0 }
        height: 40
        spacing: 2
        PlayerControlButton {
            id: subtitle_button
            objectName: "playerSubtitleButton"
            iconSource: FluentIcons.ClosedCaptionsInternational
            contentDescription: qsTr("CC 字幕（S）")
            active: controls.controller.selectedSubtitle >= 0
            onClicked: { controls.activity(); subtitle_menu.open() }
        }
        PlayerControlButton {
            caption: qsTr("弹")
            contentDescription: qsTr("弹幕（D）")
            active: controls.controller.danmakuOn
            onClicked: { controls.controller.toggleDanmaku(); controls.actionTriggered() }
            Rectangle {
                anchors { bottom: parent.bottom; bottomMargin: 3; horizontalCenter: parent.horizontalCenter }
                width: 4; height: 4; radius: 2
                color: controls.accent
                visible: controls.controller.danmakuOn
            }
        }
        PlayerControlButton {
            id: speed_button
            caption: (controls.controller.speed === 1 ? "1.0" : String(controls.controller.speed)) + "×"
            contentDescription: qsTr("播放速度")
            onClicked: { controls.activity(); speed_menu.open() }
        }
        PlayerControlButton {
            id: quality_button
            visible: controls.controller.localMedia !== true
            width: Math.min(110, Math.max(64, implicitContentWidth + 20))
            caption: controls.controller.qualityLabel || qsTr("清晰度")
            contentDescription: qsTr("清晰度与编码")
            onClicked: { controls.activity(); quality_menu.open() }
        }
        PlayerControlButton {
            id: episode_button
            visible: controls.controller.seasonMode
            caption: qsTr("选集")
            contentDescription: qsTr("选择分集")
            onClicked: {
                controls.activity()
                episode_menu.open()
                episode_list.positionViewAtIndex(Math.max(0, controls.controller.currentIndex), ListView.Contain)
            }
        }
        PlayerControlButton {
            visible: !controls.controller.seasonMode
            iconSource: FluentIcons.List
            active: controls.playlistExpanded
            contentDescription: qsTr("播放列表")
            onClicked: { controls.playlistRequested(); controls.actionTriggered() }
        }
        PlayerControlButton {
            iconSource: controls.fullscreen ? FluentIcons.BackToWindow : FluentIcons.FullScreen
            contentDescription: controls.fullscreen ? qsTr("退出全屏（F）") : qsTr("全屏（F）")
            onClicked: { controls.fullscreenRequested(); controls.actionTriggered() }
        }
    }

    DarkMenu {
        id: subtitle_menu
        objectName: "playerSubtitleMenu"
        trigger: subtitle_button
        width: Math.min(300, controls.width)
        DarkMenuItem {
            objectName: "playerSubtitleOff"
            text: qsTr("关闭字幕")
            checkable: true
            checked: controls.controller.selectedSubtitle < 0
            onTriggered: controls.controller.selectSubtitle(-1)
        }
        DarkMenuItem {
            visible: controls.controller.subtitleTracks.length === 0
            height: visible ? implicitHeight : 0
            enabled: false
            text: qsTr("没有可用字幕轨")
        }
        Repeater {
            model: controls.controller.subtitleTracks
            DarkMenuItem {
                required property int index
                required property var modelData
                text: modelData.label
                checkable: true
                checked: controls.controller.selectedSubtitle === index
                onTriggered: controls.controller.selectSubtitle(index)
            }
        }
    }
    DarkMenu {
        id: speed_menu
        objectName: "playerSpeedMenu"
        trigger: speed_button
        width: 190
        Repeater {
            model: [0.5, 0.75, 1, 1.25, 1.5, 2, 3]
            DarkMenuItem {
                required property real modelData
                text: modelData + "×"
                checkable: true
                checked: Math.abs(modelData - controls.controller.speed) < 0.001
                onTriggered: controls.controller.setSpeed(modelData)
            }
        }
        FluMenuSeparator {}
        DarkMenuItem {
            text: qsTr("保持播放速度")
            checkable: true
            checked: controls.controller.keepSpeed
            onTriggered: controls.controller.setKeepSpeed(!controls.controller.keepSpeed)
        }
    }
    DarkMenu {
        id: quality_menu
        objectName: "playerQualityMenu"
        trigger: quality_button
        DarkMenuItem {
            visible: controls.controller.qualities.length === 0
            height: visible ? implicitHeight : 0
            enabled: false
            text: qsTr("暂无可用清晰度")
        }
        Repeater {
            model: controls.controller.qualities
            DarkMenuItem {
                required property var modelData
                text: modelData.label + (modelData.needVip ? qsTr(" 会员") : "")
                enabled: modelData.available
                checkable: true
                checked: modelData.qn === controls.controller.currentQn
                onTriggered: controls.controller.setQuality(modelData.qn)
            }
        }
        FluMenuSeparator {}
        DarkMenuItem { text: qsTr("编码"); enabled: false }
        Repeater {
            model: [{ codec: "avc", label: "H.264" }, { codec: "hev1", label: "H.265" }, { codec: "av1", label: "AV1" }]
            DarkMenuItem {
                required property var modelData
                text: modelData.label
                checkable: true
                checked: modelData.codec === controls.controller.preferCodec
                onTriggered: controls.controller.setPreferCodec(modelData.codec)
            }
        }
    }
    DarkMenu {
        id: episode_menu
        objectName: "playerEpisodeMenu"
        trigger: episode_button
        width: Math.min(320, controls.width)
        contentItem: ListView {
            id: episode_list
            implicitHeight: Math.max(36, Math.min(count * 36, controls.popupMaxHeight - 12))
            model: controls.controller.playlist
            currentIndex: controls.controller.currentIndex
            clip: true
            keyNavigationEnabled: true
            reuseItems: true
            function activateCurrent() {
                if (currentIndex >= 0 && currentIndex < count) {
                    controls.controller.playByIndex(currentIndex)
                    episode_menu.close()
                }
            }
            Keys.onReturnPressed: activateCurrent()
            Keys.onEnterPressed: activateCurrent()
            Keys.onSpacePressed: activateCurrent()
            ScrollBar.vertical: FluScrollBar {}
            delegate: DarkMenuItem {
                required property int index
                required property var modelData
                width: episode_list.width
                // FluMenuItem collapses invisible rows to zero height. A closed
                // popup would then make ListView instantiate every episode.
                height: 36
                text: (index + 1) + ". " + String(modelData.title || "")
                checkable: true
                checked: index === controls.controller.currentIndex
                highlighted: ListView.isCurrentItem
                onTriggered: {
                    controls.controller.playByIndex(index)
                    episode_menu.close()
                }
            }
            FluText {
                anchors.centerIn: parent
                visible: episode_list.count === 0
                text: qsTr("暂无分集")
                color: "#aaaaaa"
            }
        }
        onOpened: {
            episode_list.currentIndex = controls.controller.currentIndex
            episode_list.forceActiveFocus()
            controls.activity()
        }
    }
}
